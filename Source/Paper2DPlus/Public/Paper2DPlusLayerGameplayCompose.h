// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Paper2DPlusLayerBakeTypes.h"
#include "Paper2DPlusTypes.h"
#include "UObject/ObjectPtr.h"

class UPaper2DPlusCharacterLayerAsset;
class UPaper2DPlusCueBase;

/**
 * One ordered, schema-neutral layer gameplay operation.
 *
 * Runtime composition and fixed publishing both adapt Layer-authored rows into this shape. Keeping the
 * merge itself independent of delivery mode prevents published output from drifting from runtime behavior.
 */
struct PAPER2DPLUS_API FPaper2DPlusLayerGameplayOperation
{
	EPaper2DPlusLayerSourceMerge AttackMerge = EPaper2DPlusLayerSourceMerge::Add;
	EPaper2DPlusLayerSourceMerge HurtMerge = EPaper2DPlusLayerSourceMerge::Add;
	EPaper2DPlusLayerSourceMerge SocketMerge = EPaper2DPlusLayerSourceMerge::Add;

	/** Key-frame-parallel authored channels. Their extent may grow a sparse baseline with default rows. */
	TArray<FFrameHitboxData> Frames;

	/** Placement order only. Null placements are ignored; semantic deduplication is deliberately not performed. */
	TArray<TObjectPtr<UPaper2DPlusCueBase>> FrameCues;
};

namespace Paper2DPlusLayerGameplayCompose
{
	/**
	 * Compose ordered operations over the character baseline.
	 *
	 * ReplaceLower is channel-local and only activates when that operation authors at least one value in the
	 * channel anywhere in the animation (the no-silent-disarm rule). A later ReplaceLower therefore removes
	 * both baseline data and lower-layer additions for that channel. Character-wide frame fields and Collision
	 * compatibility boxes survive because the accumulator begins as an exact baseline copy and those fields are
	 * never written. The result contains max(Baseline.Num(), every operation Frames.Num()) rows; rows beyond the
	 * baseline are default-initialized before layer channels are applied.
	 *
	 * @return false only when Operations is empty; in that case OutFrames is untouched.
	 */
	PAPER2DPLUS_API bool ComposeFrames(
		const TArray<FFrameHitboxData>& Baseline,
		const TArray<FPaper2DPlusLayerGameplayOperation>& Operations,
		TArray<FFrameHitboxData>& OutFrames);

	/** Baseline placements first, then each layer's non-null placements in operation order. */
	PAPER2DPLUS_API bool ComposeFrameCues(
		const TArray<TObjectPtr<UPaper2DPlusCueBase>>& Baseline,
		const TArray<FPaper2DPlusLayerGameplayOperation>& Operations,
		TArray<TObjectPtr<UPaper2DPlusCueBase>>& OutCues);
}
