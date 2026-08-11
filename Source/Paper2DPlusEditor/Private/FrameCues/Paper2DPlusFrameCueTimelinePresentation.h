// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UPaper2DPlusCueBase;

namespace Paper2DPlusFrameCueTimelinePresentation
{
	enum class EPlacementShape : uint8
	{
		CueAnchor,
		CueStateSpan
	};

	/** Pure geometry projection consumed by paint and headless tests. */
	struct FPlacementGeometry
	{
		EPlacementShape Shape = EPlacementShape::CueAnchor;
		FVector2D FillOffset = FVector2D::ZeroVector;
		FVector2D FillSize = FVector2D::ZeroVector;
		bool bDrawAnchorMark = false;
		FVector2D AnchorOffset = FVector2D::ZeroVector;
		FVector2D AnchorSize = FVector2D::ZeroVector;
		/**
		 * Moment-only trigger-edge diamond, bar-relative like AnchorOffset. It sits on the
		 * boundary the cue fires on — leading edge for FrameStart, trailing edge for FrameEnd —
		 * and REPLACES the old centered anchor tick, which pointed at neither boundary. It is
		 * also the timeline's drag handle for flipping the edge.
		 */
		bool bDrawEdgeDiamond = false;
		bool bEdgeDiamondAtEnd = false;
		FVector2D EdgeDiamondOffset = FVector2D::ZeroVector;
		FVector2D EdgeDiamondSize = FVector2D::ZeroVector;
	};

	/** Pure identity/style projection consumed by paint and headless tests. */
	struct FPlacementPresentation
	{
		FText Label;
		FLinearColor FillColor = FLinearColor::White;
		FLinearColor LabelColor = FLinearColor::Black;
		EPlacementShape Shape = EPlacementShape::CueAnchor;
		bool bShowErrorBadge = false;
	};

	// Error stays above every identity layer; selection outlines may remain above the badge.
	static constexpr int32 FillLayerOffset = 0;
	static constexpr int32 ShapeLayerOffset = 1;
	static constexpr int32 LabelLayerOffset = 2;
	static constexpr int32 ErrorBadgeLayerOffset = 3;

	/** Built-in visual convention for a fresh Cue Type's replaceable Default tag. */
	FLinearColor GetDefaultCueConventionColor();

	/** Neutral identity for an untagged, otherwise unauthored legacy placement. */
	FLinearColor GetNeutralFallbackColor();

	FPlacementPresentation ResolvePlacement(
		const UPaper2DPlusCueBase& Cue,
		bool bHasBehaviorError);

	FPlacementGeometry ResolveGeometry(
		const UPaper2DPlusCueBase& Cue,
		const FVector2D& AvailableSize);

	/** Compact hover identity for bars whose full label cannot fit inside one frame cell. */
	FText BuildToolTip(
		const UPaper2DPlusCueBase& Cue,
		const FText& BehaviorErrorDetails);

	/** Same identity/timing projection used by the timeline's screen-reader surface. */
	FText BuildAccessibleSummary(
		const UPaper2DPlusCueBase& Cue,
		const FText& BehaviorErrorDetails);
}
