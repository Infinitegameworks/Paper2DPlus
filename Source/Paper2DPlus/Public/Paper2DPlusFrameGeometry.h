// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Paper2DPlusTypes.h"

/**
 * Pure frame-geometry transforms shared by runtime collision and worldless editor previews.
 *
 * Character Profile frame boxes/sockets are authored in sprite top-left space. Runtime collision,
 * however, is evaluated around the PaperSprite pivot in Unreal X/Z space. Keep that conversion,
 * fractional-pivot compensation, facing mirror, and world-box metadata in this one runtime seam so
 * editor tools cannot silently preview different physical data.
 */
namespace Paper2DPlusFrameGeometry
{
	/**
	 * Convert one copied frame from integer top-left coordinates to integer pivot-local coordinates.
	 * Returns the fractional part of PivotLocal, which must be applied to the world origin with
	 * ApplyPivotFractionToWorldOrigin before emitting world boxes/sockets.
	 */
	PAPER2DPLUS_API FVector2D ConvertFrameDataFromTopLeftToPivotSpace(
		FFrameHitboxData& InOutFrameData,
		const FVector2D& PivotLocal);

	/** Apply the sub-pixel pivot remainder using the same facing and non-uniform scale as runtime. */
	PAPER2DPLUS_API FVector ApplyPivotFractionToWorldOrigin(
		const FVector& WorldOrigin,
		const FVector2D& PivotFraction,
		bool bFacingLeft,
		float ScaleX,
		float ScaleY);

	/** Convert one already-pivot-local hitbox to the runtime FWorldHitbox contract. */
	PAPER2DPLUS_API FWorldHitbox MakeWorldHitbox(
		const FHitboxData& Hitbox,
		const FVector& WorldOrigin,
		bool bFacingLeft,
		float ScaleX,
		float ScaleY,
		const FGameplayTag& MoveDefaultClashCategory = FGameplayTag(),
		const FGameplayTag& FrameDefenseClass = FGameplayTag());

	/** Convert one already-pivot-local socket to the runtime FWorldSocket contract. */
	PAPER2DPLUS_API FWorldSocket MakeWorldSocket(
		const FSocketData& Socket,
		const FVector& WorldOrigin,
		bool bFacingLeft,
		float ScaleX,
		float ScaleY);

	/**
	 * Convert one top-left-authored socket directly to a world location.
	 *
	 * This is the allocation-free single-socket form of the canonical frame conversion: it applies
	 * integer pivot translation, fractional-pivot compensation, facing, and nonuniform X/Z scale
	 * without copying the frame's arrays or the socket name. Invalid/non-finite input resets OutLocation.
	 */
	PAPER2DPLUS_API bool TryMakeWorldSocketLocationFromTopLeft(
		const FSocketData& Socket,
		const FVector2D& PivotLocal,
		const FVector& WorldOrigin,
		bool bFacingLeft,
		float ScaleX,
		float ScaleY,
		FVector& OutLocation);
}
