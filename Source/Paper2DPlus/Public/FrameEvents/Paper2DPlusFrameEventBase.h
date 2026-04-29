// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Paper2DPlusFrameEventBase.generated.h"

class UPaper2DPlusCharacterProfileComponent;

/** Context passed to all frame event dispatch methods. */
USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FPaper2DPlusFrameEventContext
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Frame Event")
	TObjectPtr<AActor> OwningActor = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Frame Event")
	TObjectPtr<UPaper2DPlusCharacterProfileComponent> ProfileComponent = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Frame Event")
	int32 CurrentFrame = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Frame Event")
	int32 PreviousFrame = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Frame Event")
	bool bWasLoopWrap = false;
};

/**
 * Abstract base for all frame events. Hidden from BP class pickers —
 * users subclass UPaper2DPlusFrameEvent (one-shot) or UPaper2DPlusFrameEventState (ranged).
 *
 * Virtual DispatchFrame on the base means new sibling classes don't require
 * changes to the dispatch loop in UPaper2DPlusCharacterProfileComponent::HandleFrameChanged.
 */
UCLASS(Abstract, Hidden, BlueprintType, EditInlineNew, DefaultToInstanced)
class PAPER2DPLUS_API UPaper2DPlusFrameEventBase : public UObject
{
	GENERATED_BODY()

public:
	/** Editor display color for the timeline. */
	UPROPERTY(EditAnywhere, Category = "Editor")
	FLinearColor Color = FLinearColor::White;

	/** Optional debug name shown in logs. */
	UPROPERTY(EditAnywhere, Category = "Editor")
	FName DebugName;

	/** Returns true if this event class needs the spatial editor canvas
	 *  (offset, rotation, scale gizmo). Default false. */
	virtual bool HasSpatialPreview() const { return false; }

	/** Virtual dispatch entry point called by HandleFrameChanged.
	 *  Subclasses override to implement one-shot vs ranged behavior.
	 *  ActiveRangedEvents is passed by reference so ranged subclasses can
	 *  manage their own Begin/End lifecycle in the shared tracking set.
	 *
	 *  Returns true if the dispatch actually fired (one-shot on trigger
	 *  frame, or ranged Begin/Tick/End). Returns false when the event is
	 *  in the snapshot but not firing this frame — used by the caller to
	 *  gate `OnFrameEventFired` broadcasts so BP listeners don't see
	 *  spurious every-frame signals. */
	virtual bool DispatchFrame(const FPaper2DPlusFrameEventContext& Context,
	                           TSet<TObjectPtr<UPaper2DPlusFrameEventBase>>& ActiveRangedEvents)
	{
		return false;
	}

	/** Forced-end dispatch used by the profile component when an event is
	 *  removed from the active set without a natural end (flipbook change,
	 *  stale event swept from the array). Sibling ranged subclasses override
	 *  to release any state they allocated during DispatchFrame — the default
	 *  is a no-op for one-shot events that hold no state. */
	virtual void DispatchForceEnd(const FPaper2DPlusFrameEventContext& Context) {}
};
