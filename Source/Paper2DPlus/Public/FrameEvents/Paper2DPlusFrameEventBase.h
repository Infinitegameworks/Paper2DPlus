// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Paper2DPlusNetTypes.h"
#include "Paper2DPlusFrameEventBase.generated.h"

class UPaper2DPlusCharacterProfileComponent;

/** DEPRECATED saved-Blueprint signature retained only so custom legacy graphs can load for migration inventory. */
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

	/** DEPRECATED saved-signature field. Legacy executable previews are never dispatched. */
	UPROPERTY(BlueprintReadOnly, Category = "Frame Event")
	bool bIsEditorPreview = false;

	/** DEPRECATED saved-signature field. Cues carry network policy through their own context. */
	UPROPERTY(BlueprintReadOnly, Category = "Frame Event")
	EPaper2DPlusNetContext NetContext = EPaper2DPlusNetContext::Standalone;

	/** DEPRECATED saved-signature field. Legacy executable catch-up is never dispatched. */
	UPROPERTY(BlueprintReadOnly, Category = "Frame Event")
	bool bIsCatchUp = false;

	/** Legacy test/load helper retained with the saved signature; current previews use FPaper2DPlusFrameCueContext. */
	static FPaper2DPlusFrameEventContext MakePreview(int32 InCurrentFrame, int32 InPreviousFrame, bool bInWasLoopWrap)
	{
		FPaper2DPlusFrameEventContext Ctx;
		Ctx.CurrentFrame = InCurrentFrame;
		Ctx.PreviousFrame = InPreviousFrame;
		Ctx.bWasLoopWrap = bInWasLoopWrap;
		Ctx.bIsEditorPreview = true;
		return Ctx;
	}
};

/**
 * Hidden load-only base for legacy Frame Event objects. Runtime execution and authoring were removed;
 * the class remains for one transition release so custom saved Blueprint payloads/graphs can be inventoried.
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

	/** DEPRECATED serialized policy retained so migration can preserve the equivalent Frame Cue policy. */
	UPROPERTY(EditAnywhere, Category = "Networking")
	EPaper2DPlusFrameEventNetPolicy NetPolicy = EPaper2DPlusFrameEventNetPolicy::LocalAlways;

#if WITH_EDITORONLY_DATA
	/** DEPRECATED preview preference copied to the migrated Cue. */
	UPROPERTY(EditAnywhere, Category = "Editor")
	bool bSkipInEditorPreview = false;
#endif

	/** Remap this event's frame anchor(s) under an old→new key-frame-index mapping
	 *  (OldToNew[OldIndex] = NewIndex, or INDEX_NONE if that key frame was removed). NumNewFrames is
	 *  the post-mutation key-frame count, used to clamp anchors outside the mapped range. Returns
	 *  false if the event's PRIMARY anchor frame was removed — the caller then stashes the event (on
	 *  exclude) or drops it. Base: no frame anchor → returns true (keep, unchanged). The single point
	 *  every frame-index mutation (exclude/restore/reorder) routes anchor updates through (TASK-61). */
	virtual bool RemapFrameAnchors(const TArray<int32>& OldToNew, int32 NumNewFrames) { return true; }

	/** Set this event's primary frame anchor (one-shot TriggerFrame / ranged StartFrame) directly.
	 *  Used to reattach a stashed event to a restored frame's new key-frame index. Base: no-op. */
	virtual void SetPrimaryAnchorFrame(int32 NewFrame) {}
};
