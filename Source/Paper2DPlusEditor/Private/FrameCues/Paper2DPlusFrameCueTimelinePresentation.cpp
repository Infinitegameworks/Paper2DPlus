// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "FrameCues/Paper2DPlusFrameCueTimelinePresentation.h"

#include "FrameCues/Paper2DPlusFrameCue.h"
#include "Paper2DPlusCueTags.h"
#include "Paper2DPlusSettings.h"

#define LOCTEXT_NAMESPACE "Paper2DPlusFrameCueTimelinePresentation"

namespace Paper2DPlusFrameCueTimelinePresentation
{
	namespace
	{
		bool IsAuthoredColor(const UPaper2DPlusCueBase& Cue)
		{
			// An explicit flag, never "the value differs from the default". Colour is authored through
			// an ordinary picker whose default is White, so a value test makes white unauthorable and
			// mis-reads near-white as unauthored through FLinearColor::Equals' tolerance.
			return Cue.bOverrideColor;
		}

		FText ResolveLabel(const UPaper2DPlusCueBase& Cue)
		{
			return Cue.DebugName.IsNone()
				? Cue.GetClass()->GetDisplayNameText()
				: FText::FromName(Cue.DebugName);
		}

		FLinearColor ResolveFillColor(const UPaper2DPlusCueBase& Cue)
		{
			if (IsAuthoredColor(Cue))
			{
				return Cue.Color;
			}

			bool bFoundRegistryColor = false;
			const FLinearColor RegistryColor =
				UPaper2DPlusSettings::ResolveTagColor(Cue.CueTag, bFoundRegistryColor);
			if (bFoundRegistryColor)
			{
				return RegistryColor;
			}

			if (Cue.CueTag.MatchesTagExact(Paper2DPlusCueTags::Default.GetTag()))
			{
				return GetDefaultCueConventionColor();
			}
			return GetNeutralFallbackColor();
		}

		FLinearColor ResolveContrastingTextColor(const FLinearColor& FillColor)
		{
			// Bars normally paint at 0.68 opacity over the dark timeline lane. Around a quarter
			// luminance is the practical black/white contrast crossover after that composition;
			// using the opaque-fill midpoint would put low-contrast white text on the default blue.
			return FillColor.GetLuminance() > 0.25f
				? FLinearColor::Black
				: FLinearColor::White;
		}
	}

	FLinearColor GetDefaultCueConventionColor()
	{
		// Deliberately code-owned rather than a seeded settings row. Tag Colors stays an override list.
		return FLinearColor(0.16f, 0.48f, 0.88f, 1.0f);
	}

	FLinearColor GetNeutralFallbackColor()
	{
		return FLinearColor(0.38f, 0.42f, 0.48f, 1.0f);
	}

	FPlacementPresentation ResolvePlacement(
		const UPaper2DPlusCueBase& Cue,
		const bool bHasBehaviorError)
	{
		FPlacementPresentation Result;
		Result.Label = ResolveLabel(Cue);
		Result.FillColor = ResolveFillColor(Cue);
		Result.LabelColor = ResolveContrastingTextColor(Result.FillColor);
		Result.Shape = Cue.IsRangeCue()
			? EPlacementShape::CueStateSpan
			: EPlacementShape::CueAnchor;
		Result.bShowErrorBadge = bHasBehaviorError;
		return Result;
	}

	FPlacementGeometry ResolveGeometry(
		const UPaper2DPlusCueBase& Cue,
		const FVector2D& AvailableSize)
	{
		FPlacementGeometry Result;
		if (Cue.IsRangeCue())
		{
			Result.Shape = EPlacementShape::CueStateSpan;
			Result.FillOffset = FVector2D(1.0f, 3.0f);
			Result.FillSize = FVector2D(
				FMath::Max(1.0f, AvailableSize.X - 2.0f),
				FMath::Max(2.0f, AvailableSize.Y - 6.0f));
			return Result;
		}

		Result.Shape = EPlacementShape::CueAnchor;
		Result.FillSize = AvailableSize;
		const UPaper2DPlusCue* Moment = Cast<UPaper2DPlusCue>(&Cue);
		Result.bEdgeDiamondAtEnd = Moment
			&& Moment->TriggerEdge == EPaper2DPlusCueTriggerEdge::FrameEnd;
		Result.bDrawEdgeDiamond = true;
		Result.EdgeDiamondSize = FVector2D(
			FMath::Min(7.0f, FMath::Max(3.0f, AvailableSize.X - 2.0f)),
			FMath::Min(7.0f, FMath::Max(3.0f, AvailableSize.Y - 2.0f)));
		Result.EdgeDiamondOffset = FVector2D(
			Result.bEdgeDiamondAtEnd
				? FMath::Max(0.0f, AvailableSize.X - Result.EdgeDiamondSize.X - 1.5f)
				: 1.5f,
			FMath::Max(0.0f, (AvailableSize.Y - Result.EdgeDiamondSize.Y) * 0.5f));
		return Result;
	}

	FText BuildToolTip(
		const UPaper2DPlusCueBase& Cue,
		const FText& BehaviorErrorDetails)
	{
		const FText CueName = ResolveLabel(Cue);
		const UPaper2DPlusCue* TooltipMoment = Cast<UPaper2DPlusCue>(&Cue);
		const bool bTooltipFiresAtEnd = TooltipMoment
			&& TooltipMoment->TriggerEdge == EPaper2DPlusCueTriggerEdge::FrameEnd;
		const FText TimingSummary = Cue.IsRangeCue()
			? FText::Format(
				LOCTEXT(
					"TimelineCueStateToolTip",
					"Cue State {0}. Start frame {1}; duration {2} frames."),
				CueName,
				FText::AsNumber(Cue.GetPrimaryAnchorFrame()),
				FText::AsNumber(Cue.GetCueFrameCount()))
			: FText::Format(
				bTooltipFiresAtEnd
					? LOCTEXT(
						"TimelineCueToolTipEndEdge",
						"Cue {0}. Frame {1}, fires when the frame ends.")
					: LOCTEXT(
						"TimelineCueToolTip",
						"Cue {0}. Frame {1}."),
				CueName,
				FText::AsNumber(Cue.GetPrimaryAnchorFrame()));
		return BehaviorErrorDetails.IsEmpty()
			? TimingSummary
			: FText::Format(
				LOCTEXT(
					"TimelineCueToolTipBehaviorErrorSuffix",
					"{0} Behavior error: {1}"),
				TimingSummary,
				BehaviorErrorDetails);
	}

	FText BuildAccessibleSummary(
		const UPaper2DPlusCueBase& Cue,
		const FText& BehaviorErrorDetails)
	{
		const FText CueName = ResolveLabel(Cue);
		const UPaper2DPlusCue* SummaryMoment = Cast<UPaper2DPlusCue>(&Cue);
		const bool bSummaryFiresAtEnd = SummaryMoment
			&& SummaryMoment->TriggerEdge == EPaper2DPlusCueTriggerEdge::FrameEnd;
		const FText TimingSummary = Cue.IsRangeCue()
			? FText::Format(
				LOCTEXT(
					"TimelineAccessibleCueState",
					"Frame Cue timeline. Selected Cue State {0}, start frame {1}, duration {2} frames. Left and Right move; Shift plus Left or Right resizes; Alt plus Up or Down changes track; Control D duplicates; Delete removes."),
				CueName,
				FText::AsNumber(Cue.GetPrimaryAnchorFrame()),
				FText::AsNumber(Cue.GetCueFrameCount()))
			: FText::Format(
				bSummaryFiresAtEnd
					? LOCTEXT(
						"TimelineAccessibleCueEndEdge",
						"Frame Cue timeline. Selected Cue {0}, frame {1}, fires when the frame ends. Drag the diamond to either edge to change the trigger edge. Left and Right move; Alt plus Up or Down changes track; Control D duplicates; Delete removes.")
					: LOCTEXT(
						"TimelineAccessibleCue",
						"Frame Cue timeline. Selected Cue {0}, frame {1}. Drag the diamond to either edge to change the trigger edge. Left and Right move; Alt plus Up or Down changes track; Control D duplicates; Delete removes."),
				CueName,
				FText::AsNumber(Cue.GetPrimaryAnchorFrame()));

		return BehaviorErrorDetails.IsEmpty()
			? TimingSummary
			: FText::Format(
				LOCTEXT(
					"TimelineAccessibleBehaviorErrorSuffix",
					"{0} Behavior error: {1}"),
				TimingSummary,
				BehaviorErrorDetails);
	}
}

#undef LOCTEXT_NAMESPACE
