// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "SDirectionalAnimationWheel.h"

#include "EditorCanvasUtils.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Styling/StyleColors.h"
#include "Widgets/Accessibility/SlateAccessibleMessageHandler.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SConstraintCanvas.h"

#define LOCTEXT_NAMESPACE "SDirectionalAnimationWheel"

namespace Paper2DPlusDirectionalWheelPrivate
{
	constexpr float DesiredWheelSize = 240.0f;
	constexpr double InnerRadiusFraction = 0.22;
	constexpr double OuterRadiusFraction = 0.48;
	constexpr float SegmentControlSize = 20.0f;

	// House palette. Structural colors come from the editor theme tokens; the two domain accents
	// reuse the plugin's established constants (selection green = FFrameStripCellArgs::
	// SelectedBorderColor, hover blue = the extractor-canvas hover outline) so the wheel reads
	// like the rest of the editor instead of inventing its own vocabulary.
	FLinearColor ChromeColor()
	{
		return FStyleColors::Foreground.GetSpecifiedColor();
	}
	FLinearColor InvalidColor()
	{
		return FStyleColors::Error.GetSpecifiedColor();
	}
	FLinearColor FocusColor()
	{
		return FStyleColors::Primary.GetSpecifiedColor();
	}
	const FLinearColor SelectionGreen(0.20f, 0.60f, 0.30f, 1.0f);
	const FLinearColor HoverBlue(0.5f, 0.8f, 1.0f, 1.0f);
	const FLinearColor EmptyGrey(0.45f, 0.45f, 0.5f, 0.9f);

	FSlateFontInfo SlotLabelFont()
	{
		return FAppStyle::GetFontStyle(TEXT("PropertyWindow.BoldFont"));
	}
	FSlateFontInfo CenterLabelFont()
	{
		return FAppStyle::GetFontStyle(TEXT("SmallFont"));
	}

	FVector2D MeasureLabel(const FString& Label, const FSlateFontInfo& Font)
	{
		if (!FSlateApplication::IsInitialized())
		{
			return FVector2D(Label.Len() * 6.0, 12.0);
		}
		return FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(
			Label, Font);
	}

	/** Chord count scaled to arc length so 3-way wheels stay round and 16-way wheels stay cheap. */
	int32 ArcStepsForSeparation(double SeparationDegrees)
	{
		return FMath::Clamp(FMath::CeilToInt(SeparationDegrees / 6.0), 4, 24);
	}

	double NormalizeBearing(double BearingDegrees)
	{
		double Wrapped = FMath::Fmod(BearingDegrees, 360.0);
		if (Wrapped < 0.0)
		{
			Wrapped += 360.0;
		}
		return Wrapped;
	}

	FVector2D ScreenUnitFromBearing(double BearingDegrees)
	{
		const double Radians = FMath::DegreesToRadians(BearingDegrees);
		// Directional space is +Y-up; Slate local space is +Y-down.
		return FVector2D(FMath::Sin(Radians), -FMath::Cos(Radians));
	}

	bool AreCachedOffsetsEquivalent(float Left, float Right)
	{
		const bool bLeftFinite = FMath::IsFinite(Left);
		const bool bRightFinite = FMath::IsFinite(Right);
		return bLeftFinite && bRightFinite ? Left == Right : bLeftFinite == bRightFinite;
	}

	/** Points along one bearing arc at a fixed radius, inset by an angular gap on each end. */
	TArray<FVector2D> BuildArcPoints(
		const FVector2D& Center,
		double Radius,
		double StartBearing,
		double EndBearing,
		double EndInsetDegrees)
	{
		TArray<FVector2D> Points;
		const double InsetStart = StartBearing + EndInsetDegrees;
		const double InsetEnd = EndBearing - EndInsetDegrees;
		if (InsetEnd <= InsetStart)
		{
			return Points;
		}
		const int32 Steps = ArcStepsForSeparation(InsetEnd - InsetStart);
		Points.Reserve(Steps + 1);
		for (int32 Step = 0; Step <= Steps; ++Step)
		{
			const double Bearing = FMath::Lerp(
				InsetStart, InsetEnd, static_cast<double>(Step) / Steps);
			Points.Add(Center + ScreenUnitFromBearing(Bearing) * Radius);
		}
		return Points;
	}
}

void SDirectionalAnimationWheel::Construct(const FArguments& InArgs)
{
	DirectionCount = InArgs._DirectionCount;
	AngleOffsetDegrees = InArgs._AngleOffsetDegrees;
	SegmentStates = InArgs._SegmentStates;
	CurrentSelection = InArgs._CurrentSelection;
	BaseAnimationText = InArgs._BaseAnimationText;
	DirectCommitEnabled = InArgs._DirectCommitEnabled;
	OnHoveredSlotChanged = InArgs._OnHoveredSlotChanged;
	OnSlotCommitted = InArgs._OnSlotCommitted;
	OnCancelled = InArgs._OnCancelled;

	ChildSlot
	[
		SAssignNew(SegmentCanvas, SConstraintCanvas)
	];

	SetClipping(EWidgetClipping::ClipToBounds);
	// The tooltip tracks the ring-wide painted hover, so any point on a wedge names that slot's
	// assignment — not just the small focusable segment control at the label.
	SetToolTipText(TAttribute<FText>::CreateSP(
		this, &SDirectionalAnimationWheel::GetDynamicToolTipText));

	RefreshCachedPresentation();
	CachedCurrentSelection = CurrentSelection.Get(INDEX_NONE);
	CachedBaseAnimationText = BaseAnimationText.Get(FText::GetEmpty());
	const FString BaseAnimationLabel = CachedBaseAnimationText.ToString();
	CachedPaintBaseAnimationLabel = BaseAnimationLabel.Len() > 18
		? BaseAnimationLabel.Left(17) + TEXT("…")
		: BaseAnimationLabel;
	CachedCenterLabelSize = Paper2DPlusDirectionalWheelPrivate::MeasureLabel(
		CachedPaintBaseAnimationLabel.IsEmpty()
			? FString(TEXT("Base"))
			: CachedPaintBaseAnimationLabel,
		Paper2DPlusDirectionalWheelPrivate::CenterLabelFont());
	RebuildCachedGeometry(FVector2D(
		Paper2DPlusDirectionalWheelPrivate::DesiredWheelSize,
		Paper2DPlusDirectionalWheelPrivate::DesiredWheelSize));

#if WITH_ACCESSIBILITY
	SetCanChildrenBeAccessible(true);
	SetAccessibleBehavior(
		EAccessibleBehavior::Custom,
		TAttribute<FText>::CreateSP(this, &SDirectionalAnimationWheel::GetAccessibleSummaryText));
#endif
}

FVector2D SDirectionalAnimationWheel::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	(void)LayoutScaleMultiplier;
	return FVector2D(
		Paper2DPlusDirectionalWheelPrivate::DesiredWheelSize,
		Paper2DPlusDirectionalWheelPrivate::DesiredWheelSize);
}

int32 SDirectionalAnimationWheel::OnPaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	bool bParentEnabled) const
{
	const bool bEnabled = ShouldBeEnabled(bParentEnabled);
	const ESlateDrawEffect DrawEffects = bEnabled
		? ESlateDrawEffect::None
		: ESlateDrawEffect::DisabledEffect;
	const FLinearColor Tint = InWidgetStyle.GetColorAndOpacityTint();
	const FSlateBrush* WhiteBrush = FAppStyle::Get().GetBrush(TEXT("WhiteBrush"));
	const FVector2D LocalSize = AllottedGeometry.GetLocalSize();
	const FVector2D Center = LocalSize * 0.5;
	const double MinDimension = FMath::Min(LocalSize.X, LocalSize.Y);
	const double OuterRadius =
		MinDimension * Paper2DPlusDirectionalWheelPrivate::OuterRadiusFraction;
	int32 DrawLayer = LayerId;

	// Round backing disc in the theme's recessed color. The former full-rect near-black plate read
	// as a black square punched into the menu; the popup corners now keep the menu background. A
	// rounded-box brush with corner radius = half its box renders an exact anti-aliased circle in
	// one pass (a thick stroked polyline self-overlaps at this thickness-to-segment ratio).
	if (PlateBrush.IsValid())
	{
		const double PlateRadius = OuterRadius + 8.0;
		// MakeBox does NOT apply a brush's own TintColor (that is SImage behavior) — the fill must
		// arrive through the element tint, or the disc renders white regardless of the theme token.
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			DrawLayer++,
			MakePaintGeometry(
				AllottedGeometry,
				FVector2D(PlateRadius * 2.0, PlateRadius * 2.0),
				FSlateLayoutTransform(Center - FVector2D(PlateRadius, PlateRadius))),
			PlateBrush.Get(),
			DrawEffects,
			FStyleColors::Recessed.GetSpecifiedColor().CopyWithNewOpacity(0.96f) * Tint);
	}

	for (const FCachedSegmentGeometry& Segment : CachedGeometry)
	{
		if (!CachedPresentation.IsValidIndex(Segment.SlotIndex))
		{
			continue;
		}
		const FCachedSegmentPresentation& Presentation = CachedPresentation[Segment.SlotIndex];
		const bool bSelected = Segment.SlotIndex == CachedCurrentSelection;
		const bool bHovered = Segment.SlotIndex == HoveredSlotIndex;
		const bool bFocused = IsSegmentControlFocused(Segment.SlotIndex);
		const bool bPointed = bHovered || bFocused;
		const FLinearColor StateColor = !Presentation.bValid
			? Paper2DPlusDirectionalWheelPrivate::InvalidColor()
			: (Presentation.bOccupied
				? Paper2DPlusDirectionalWheelPrivate::SelectionGreen
				: Paper2DPlusDirectionalWheelPrivate::EmptyGrey);

		// Filled wedge body: occupancy reads at a glance instead of from a 1.25px hairline. Hover
		// and selection brighten the same band so pointing at the current selection still responds.
		float FillAlpha = !Presentation.bValid
			? 0.16f
			: (Presentation.bOccupied ? 0.20f : 0.05f);
		if (bSelected)
		{
			FillAlpha += 0.18f;
		}
		if (bPointed)
		{
			FillAlpha += 0.06f;
		}
		const FLinearColor FillColor = Presentation.bValid && !Presentation.bOccupied
			? FLinearColor(1.0f, 1.0f, 1.0f, FillAlpha)
			: StateColor.CopyWithNewOpacity(FillAlpha);
		if (Segment.FillPoints.Num() > 1)
		{
			FSlateDrawElement::MakeLines(
				OutDrawElements,
				DrawLayer,
				AllottedGeometry.ToPaintGeometry(),
				Segment.FillPoints,
				DrawEffects,
				FillColor * Tint,
				true,
				Segment.FillThickness);
		}

		// The state outline is never replaced — selection, hover, and keyboard focus all compose
		// ON TOP of it, so an invalid wedge keeps its warning color under every transient AND
		// persistent state. Selection (the committed preview bearing) is its own dimension in the
		// theme's primary accent; pointing (hover or segment focus) is the house hover blue.
		FSlateDrawElement::MakeLines(
			OutDrawElements,
			DrawLayer + 1,
			AllottedGeometry.ToPaintGeometry(),
			Segment.OutlinePoints,
			DrawEffects,
			StateColor * Tint,
			true,
			1.0f);
		if (bSelected)
		{
			FSlateDrawElement::MakeLines(
				OutDrawElements, DrawLayer + 1, AllottedGeometry.ToPaintGeometry(),
				Segment.OutlinePoints,
				DrawEffects,
				Paper2DPlusDirectionalWheelPrivate::FocusColor() * Tint, true, 2.5f);
		}
		if (bPointed)
		{
			FSlateDrawElement::MakeLines(
				OutDrawElements, DrawLayer + 2, AllottedGeometry.ToPaintGeometry(),
				Segment.OutlinePoints,
				DrawEffects, Paper2DPlusDirectionalWheelPrivate::HoverBlue * Tint, true, 2.0f);
		}

		const FVector2D IconSize(9.0, 9.0);
		if (!Presentation.bValid)
		{
			FSlateDrawElement::MakeLines(
				OutDrawElements, DrawLayer + 2, AllottedGeometry.ToPaintGeometry(),
				Segment.InvalidSlashA,
				DrawEffects, StateColor * Tint, true, 2.0f);
			FSlateDrawElement::MakeLines(
				OutDrawElements, DrawLayer + 2, AllottedGeometry.ToPaintGeometry(),
				Segment.InvalidSlashB,
				DrawEffects, StateColor * Tint, true, 2.0f);
		}
		else if (Presentation.bOccupied)
		{
			FSlateDrawElement::MakeBox(
				OutDrawElements,
				DrawLayer + 2,
				MakePaintGeometry(
					AllottedGeometry, IconSize, FSlateLayoutTransform(Segment.IconPosition)),
				WhiteBrush,
				DrawEffects,
				StateColor * Tint);
		}
		else
		{
			FSlateDrawElement::MakeLines(
				OutDrawElements, DrawLayer + 2, AllottedGeometry.ToPaintGeometry(),
				Segment.EmptyDiamond,
				DrawEffects, StateColor * Tint, true, 1.5f);
		}

		FSlateDrawElement::MakeText(
			OutDrawElements,
			DrawLayer + 3,
			MakePaintGeometry(
				AllottedGeometry,
				Segment.LabelSize,
				FSlateLayoutTransform(Segment.LabelCenter - Segment.LabelSize * 0.5)),
			Segment.SlotLabel,
			Paper2DPlusDirectionalWheelPrivate::SlotLabelFont(),
			DrawEffects,
			(bSelected
				? Paper2DPlusDirectionalWheelPrivate::ChromeColor()
				: StateColor) * Tint);

		if (bSelected)
		{
			FSlateDrawElement::MakeLines(
				OutDrawElements, DrawLayer + 3, AllottedGeometry.ToPaintGeometry(),
				Segment.SelectionMarker,
				DrawEffects,
				Paper2DPlusDirectionalWheelPrivate::FocusColor() * Tint, true, 2.0f);
		}
	}
	DrawLayer += 4;

	if (CachedCenterCircle.Num() > 1)
	{
		FSlateDrawElement::MakeLines(
			OutDrawElements,
			DrawLayer,
			AllottedGeometry.ToPaintGeometry(),
			CachedCenterCircle,
			DrawEffects,
			Paper2DPlusDirectionalWheelPrivate::ChromeColor().CopyWithNewOpacity(0.55f) * Tint,
			true,
			1.5f);

		// Fixed north tick outside the rotating ring: the one world-space anchor that explains a
		// nonzero angle offset instead of silently rotating "slot 0 is up" out from under the user.
		const TArray<FVector2D> NorthTick =
		{
			Center + FVector2D(0.0, -(OuterRadius + 1.0)),
			Center + FVector2D(0.0, -(OuterRadius + 7.0))
		};
		FSlateDrawElement::MakeLines(
			OutDrawElements, DrawLayer, AllottedGeometry.ToPaintGeometry(),
			NorthTick,
			DrawEffects,
			Paper2DPlusDirectionalWheelPrivate::ChromeColor() * Tint, true, 2.0f);
	}

	if (!IsSupportedTopology())
	{
		// The sighted failure state matches the accessible one instead of a featureless plate.
		const FString TopologyMessage = LOCTEXT(
			"PaintInvalidTopology",
			"Direction wheel unavailable.\nThe direction count or angle offset is invalid.").ToString();
		const FVector2D MessageSize = Paper2DPlusDirectionalWheelPrivate::MeasureLabel(
			TopologyMessage, Paper2DPlusDirectionalWheelPrivate::CenterLabelFont());
		FSlateDrawElement::MakeText(
			OutDrawElements,
			DrawLayer + 1,
			MakePaintGeometry(
				AllottedGeometry,
				MessageSize,
				FSlateLayoutTransform(Center - MessageSize * 0.5)),
			TopologyMessage,
			Paper2DPlusDirectionalWheelPrivate::CenterLabelFont(),
			DrawEffects,
			FStyleColors::Warning.GetSpecifiedColor() * Tint);
	}
	else
	{
		const FString CenterLabel = CachedPaintBaseAnimationLabel.IsEmpty()
			? FString(TEXT("Base"))
			: CachedPaintBaseAnimationLabel;
		const FVector2D NameSize = CachedCenterLabelSize.IsNearlyZero()
			? Paper2DPlusDirectionalWheelPrivate::MeasureLabel(
				CenterLabel, Paper2DPlusDirectionalWheelPrivate::CenterLabelFont())
			: CachedCenterLabelSize;
		FSlateDrawElement::MakeText(
			OutDrawElements,
			DrawLayer + 1,
			MakePaintGeometry(
				AllottedGeometry,
				NameSize,
				FSlateLayoutTransform(
					Center - FVector2D(NameSize.X * 0.5, NameSize.Y + 1.0))),
			CenterLabel,
			Paper2DPlusDirectionalWheelPrivate::CenterLabelFont(),
			DrawEffects,
			Paper2DPlusDirectionalWheelPrivate::ChromeColor() * Tint);

		// Second hub line: the pointed slot's assignment while hovering, otherwise authored
		// coverage (plus the active offset, which otherwise rotates the ring unexplained).
		FString InfoLine;
		FLinearColor InfoColor =
			Paper2DPlusDirectionalWheelPrivate::ChromeColor().CopyWithNewOpacity(0.7f);
		if (CachedPresentation.IsValidIndex(HoveredSlotIndex))
		{
			const FCachedSegmentPresentation& Hovered = CachedPresentation[HoveredSlotIndex];
			const FString SlotName = GetSlotDirectionLabel(
				HoveredSlotIndex, GetDirectionCount(), GetAngleOffsetDegrees());
			if (!Hovered.bValid)
			{
				InfoLine = FString::Printf(TEXT("%s · %s"), *SlotName,
					*LOCTEXT("PaintHoverInvalid", "Invalid — see Details").ToString());
				InfoColor = Paper2DPlusDirectionalWheelPrivate::InvalidColor();
			}
			else if (Hovered.bOccupied)
			{
				InfoLine = FString::Printf(
					TEXT("%s · %s"), *SlotName, *Hovered.AssetLabel.ToString());
			}
			else
			{
				InfoLine = FString::Printf(TEXT("%s · %s"), *SlotName,
					*LOCTEXT("PaintHoverEmpty", "Empty — no fallback").ToString());
			}
		}
		else
		{
			int32 OccupiedCount = 0;
			for (const FCachedSegmentPresentation& Presentation : CachedPresentation)
			{
				OccupiedCount += Presentation.bOccupied ? 1 : 0;
			}
			InfoLine = FString::Printf(
				TEXT("%d / %d %s"), OccupiedCount, CachedPresentation.Num(),
				*LOCTEXT("PaintCoverageAssigned", "assigned").ToString());
			const float Offset = GetAngleOffsetDegrees();
			if (!FMath::IsNearlyZero(Offset))
			{
				InfoLine += FString::Printf(TEXT(" · %+.1f°"), Offset);
			}
		}
		const FVector2D InfoSize = Paper2DPlusDirectionalWheelPrivate::MeasureLabel(
			InfoLine, Paper2DPlusDirectionalWheelPrivate::CenterLabelFont());
		FSlateDrawElement::MakeText(
			OutDrawElements,
			DrawLayer + 1,
			MakePaintGeometry(
				AllottedGeometry,
				InfoSize,
				FSlateLayoutTransform(Center - FVector2D(InfoSize.X * 0.5, -1.0))),
			InfoLine,
			Paper2DPlusDirectionalWheelPrivate::CenterLabelFont(),
			DrawEffects,
			InfoColor * Tint);
	}
	DrawLayer += 2;
	DrawLayer = FMath::Max(
		DrawLayer,
		SCompoundWidget::OnPaint(
			Args,
			AllottedGeometry,
			MyCullingRect,
			OutDrawElements,
			DrawLayer,
			InWidgetStyle,
			bParentEnabled));
	++DrawLayer;

	// Focus-visible rule (the Effect Preview precedent): keyboard/programmatic focus shows a slim
	// themed ring at the rim; mouse focus never leaves a standing frame, and nothing draws the
	// widget's rectangular bounds on a radial control.
	if (HasAnyUserFocusOrFocusedDescendants() && LastFocusCause != EFocusCause::Mouse)
	{
		constexpr int32 RingSteps = 48;
		const double RingRadius = OuterRadius + 2.5;
		TArray<FVector2D> FocusRing;
		FocusRing.Reserve(RingSteps + 1);
		for (int32 Step = 0; Step <= RingSteps; ++Step)
		{
			const double Bearing = 360.0 * static_cast<double>(Step) / RingSteps;
			FocusRing.Add(
				Center
				+ Paper2DPlusDirectionalWheelPrivate::ScreenUnitFromBearing(Bearing)
					* RingRadius);
		}
		FSlateDrawElement::MakeLines(
			OutDrawElements,
			DrawLayer,
			AllottedGeometry.ToPaintGeometry(),
			FocusRing,
			DrawEffects,
			Paper2DPlusDirectionalWheelPrivate::FocusColor() * Tint,
			true,
			1.5f);
		++DrawLayer;
	}
	return DrawLayer;
}

void SDirectionalAnimationWheel::Tick(
	const FGeometry& AllottedGeometry,
	double InCurrentTime,
	float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	bool bChanged = false;
	const FVector2D NewLocalSize = AllottedGeometry.GetLocalSize();
	const int32 NewDirectionCount = GetDirectionCount();
	const float NewAngleOffsetDegrees = GetAngleOffsetDegrees();
	if (!CachedLocalSize.Equals(NewLocalSize, KINDA_SMALL_NUMBER)
		|| CachedDirectionCount != NewDirectionCount
		|| !Paper2DPlusDirectionalWheelPrivate::AreCachedOffsetsEquivalent(
			CachedAngleOffsetDegrees, NewAngleOffsetDegrees))
	{
		RebuildCachedGeometry(NewLocalSize);
		bChanged = true;
	}
	bChanged |= RefreshCachedPresentation();

	const int32 NewSelection = CurrentSelection.Get(INDEX_NONE);
	if (NewSelection != CachedCurrentSelection)
	{
		CachedCurrentSelection = NewSelection;
		bChanged = true;
	}
	const int32 NewFocusedSlot = GetFocusedSegmentSlot();
	if (NewFocusedSlot != CachedFocusedSegmentSlot)
	{
		CachedFocusedSegmentSlot = NewFocusedSlot;
		bChanged = true;
	}
	const FText NewBaseText = BaseAnimationText.Get(FText::GetEmpty());
	if (!NewBaseText.EqualTo(CachedBaseAnimationText))
	{
		CachedBaseAnimationText = NewBaseText;
		const FString NewBaseLabel = NewBaseText.ToString();
		CachedPaintBaseAnimationLabel = NewBaseLabel.Len() > 18
			? NewBaseLabel.Left(17) + TEXT("…")
			: NewBaseLabel;
		CachedCenterLabelSize = Paper2DPlusDirectionalWheelPrivate::MeasureLabel(
			CachedPaintBaseAnimationLabel.IsEmpty()
				? FString(TEXT("Base"))
				: CachedPaintBaseAnimationLabel,
			Paper2DPlusDirectionalWheelPrivate::CenterLabelFont());
		bChanged = true;
	}
	if (bChanged)
	{
		Invalidate(EInvalidateWidgetReason::Paint);
		NotifyAccessibleStateChanged();
	}
}

FReply SDirectionalAnimationWheel::OnFocusReceived(
	const FGeometry& MyGeometry,
	const FFocusEvent& InFocusEvent)
{
	(void)MyGeometry;
	LastFocusCause = InFocusEvent.GetCause();
	Invalidate(EInvalidateWidgetReason::Paint);
	FReply Reply = FReply::Handled();
	if (!IsDirectCommitEnabled())
	{
		// A held shortcut has no mouse-button gesture to establish capture. Capture as soon as the
		// popup receives focus so pointer intent continues to update outside the wheel and any
		// stolen/lost capture becomes an explicit cancellation path.
		Reply.CaptureMouse(SharedThis(this));
	}
	return Reply;
}

void SDirectionalAnimationWheel::OnFocusLost(const FFocusEvent& InFocusEvent)
{
	SCompoundWidget::OnFocusLost(InFocusEvent);
	CachedFocusedSegmentSlot = INDEX_NONE;
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SDirectionalAnimationWheel::OnFocusChanging(
	const FWeakWidgetPath& PreviousFocusPath,
	const FWidgetPath& NewWidgetPath,
	const FFocusEvent& InFocusEvent)
{
	if (!bInteractionResolved
		&& PreviousFocusPath.ContainsWidget(this)
		&& !NewWidgetPath.ContainsWidget(this))
	{
		CachedFocusedSegmentSlot = INDEX_NONE;
		Invalidate(EInvalidateWidgetReason::Paint);
		SetHoveredSlot(INDEX_NONE);
		// The host defers restoration until Slate completes this focus transition.
		CancelInteraction(/*bRestoreFocus=*/true);
	}
	SCompoundWidget::OnFocusChanging(PreviousFocusPath, NewWidgetPath, InFocusEvent);
}

FReply SDirectionalAnimationWheel::OnKeyDown(
	const FGeometry& MyGeometry,
	const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::Left || Key == EKeys::Up)
	{
		return MakeSegmentFocusReply(
			TraverseSlotIndex(GetKeyboardAnchorSlot(), -1, GetDirectionCount()));
	}
	if (Key == EKeys::Right || Key == EKeys::Down)
	{
		return MakeSegmentFocusReply(
			TraverseSlotIndex(GetKeyboardAnchorSlot(), 1, GetDirectionCount()));
	}
	if (Key == EKeys::Home && IsSupportedTopology())
	{
		return MakeSegmentFocusReply(0);
	}
	if (Key == EKeys::End && IsSupportedTopology())
	{
		return MakeSegmentFocusReply(GetDirectionCount() - 1);
	}
	if (Key == EKeys::Escape)
	{
		CancelInteraction();
		return FReply::Handled();
	}

	// A held shortcut keeps its command modifier down while this wheel owns focus. Navigation and
	// cancellation above remain wheel-local; unrelated modified commands (including Enter/Space)
	// still yield to Slate and the editor command list.
	if (InKeyEvent.IsControlDown() || InKeyEvent.IsAltDown() || InKeyEvent.IsCommandDown())
	{
		return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
	}
	if (Key == EKeys::Enter || Key == EKeys::SpaceBar)
	{
		if (IsDirectCommitEnabled())
		{
			const int32 CommitIndex = CachedPresentation.IsValidIndex(HoveredSlotIndex)
				? HoveredSlotIndex
				: GetKeyboardAnchorSlot();
			CommitSlot(CommitIndex);
		}
		return FReply::Handled();
	}
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

FReply SDirectionalAnimationWheel::OnMouseMove(
	const FGeometry& MyGeometry,
	const FPointerEvent& MouseEvent)
{
	const FVector2D LocalPoint = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	SetHoveredSlot(HitTestSegment(
		LocalPoint,
		MyGeometry.GetLocalSize(),
		GetDirectionCount(),
		GetAngleOffsetDegrees()));
	return FReply::Unhandled();
}

void SDirectionalAnimationWheel::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseLeave(MouseEvent);
	SetHoveredSlot(INDEX_NONE);
}

FReply SDirectionalAnimationWheel::OnMouseButtonDown(
	const FGeometry& MyGeometry,
	const FPointerEvent& MouseEvent)
{
	const FVector2D LocalPoint = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	const int32 HitSlot = HitTestSegment(
		LocalPoint,
		MyGeometry.GetLocalSize(),
		GetDirectionCount(),
		GetAngleOffsetDegrees());
	if (!IsDirectCommitEnabled() && HitSlot == INDEX_NONE)
	{
		// A held wheel owns pointer capture, so outside-ring clicks route here instead of through
		// the popup's normal outside-click dismissal. Resolve that gesture as cancellation and
		// explicitly release the capture established when the wheel received focus.
		SetHoveredSlot(INDEX_NONE);
		CancelInteraction();
		return FReply::Handled().ReleaseMouseCapture();
	}
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}
	SetHoveredSlot(HitSlot);
	if (ResolveDirectPointerAction(HitSlot))
	{
		// Commit and cancellation can synchronously dismiss the popup. Never return focus into that
		// detached tree.
		return FReply::Handled();
	}
	return FReply::Handled().SetUserFocus(SharedThis(this), EFocusCause::Mouse);
}

void SDirectionalAnimationWheel::OnMouseCaptureLost(
	const FCaptureLostEvent& CaptureLostEvent)
{
	SCompoundWidget::OnMouseCaptureLost(CaptureLostEvent);
	if (bInteractionResolved)
	{
		return;
	}
	bInteractionResolved = true;
	bCaptureLossPending = true;
	SetHoveredSlot(INDEX_NONE);

	// Window activation releases capture before Slate tells the menu stack about the external
	// dismissal. Defer one Slate turn so that host-owned dismissal can disarm this wheel and keep
	// the clicked window's focus; a standalone capture loss still cancels and restores the opener.
	const TWeakPtr<SDirectionalAnimationWheel> WeakWheel = SharedThis(this);
	RegisterActiveTimer(
		0.0f,
		FWidgetActiveTimerDelegate::CreateLambda([WeakWheel](double, float)
		{
			if (const TSharedPtr<SDirectionalAnimationWheel> Wheel = WeakWheel.Pin();
				Wheel.IsValid() && Wheel->bCaptureLossPending)
			{
				Wheel->bCaptureLossPending = false;
				Wheel->OnCancelled.ExecuteIfBound(/*bRestoreFocus=*/true);
			}
			return EActiveTimerReturnType::Stop;
		}));
}

bool SDirectionalAnimationWheel::CommitHoveredSlot()
{
	return CommitSlot(HoveredSlotIndex);
}

bool SDirectionalAnimationWheel::CommitHeldSelection()
{
	if (HoveredSlotIndex != INDEX_NONE)
	{
		return CommitSlot(HoveredSlotIndex);
	}
	// Pointer intent wins even when the pointed segment is invalid. Only a release with no live
	// hover may fall back to an explicitly keyboard-focused segment.
	return CommitSlot(GetFocusedSegmentSlot());
}

void SDirectionalAnimationWheel::CancelInteraction(bool bRestoreFocus)
{
	if (bInteractionResolved)
	{
		return;
	}
	bInteractionResolved = true;
	OnCancelled.ExecuteIfBound(bRestoreFocus);
}

void SDirectionalAnimationWheel::ResolveForHostTeardown()
{
	bInteractionResolved = true;
	bCaptureLossPending = false;
}

int32 SDirectionalAnimationWheel::HitTestSegment(
	const FVector2D& LocalPoint,
	const FVector2D& LocalSize,
	int32 InDirectionCount,
	float InAngleOffsetDegrees)
{
	if (LocalSize.X <= 0.0 || LocalSize.Y <= 0.0)
	{
		return INDEX_NONE;
	}
	const double MinDimension = FMath::Min(LocalSize.X, LocalSize.Y);
	const FVector2D Delta = LocalPoint - LocalSize * 0.5;
	const double Radius = Delta.Size();
	if (Radius < MinDimension * Paper2DPlusDirectionalWheelPrivate::InnerRadiusFraction
		|| Radius > MinDimension * Paper2DPlusDirectionalWheelPrivate::OuterRadiusFraction)
	{
		return INDEX_NONE;
	}

	int32 SlotIndex = INDEX_NONE;
	const FVector2D DirectionalVector(Delta.X, -Delta.Y);
	return UPaper2DPlusCharacterProfileAsset::ResolveDirectionalSlotIndex(
		DirectionalVector,
		InDirectionCount,
		InAngleOffsetDegrees,
		SlotIndex)
		? SlotIndex
		: INDEX_NONE;
}

int32 SDirectionalAnimationWheel::TraverseSlotIndex(
	int32 CurrentIndex,
	int32 Delta,
	int32 InDirectionCount)
{
	if (InDirectionCount < UPaper2DPlusCharacterProfileAsset::MinimumDirectionalCount
		|| InDirectionCount > UPaper2DPlusCharacterProfileAsset::MaximumDirectionalCount)
	{
		return INDEX_NONE;
	}
	const int32 BaseIndex = CurrentIndex >= 0 && CurrentIndex < InDirectionCount
		? CurrentIndex
		: 0;
	int32 Wrapped = (BaseIndex + Delta) % InDirectionCount;
	if (Wrapped < 0)
	{
		Wrapped += InDirectionCount;
	}
	return Wrapped;
}

FString SDirectionalAnimationWheel::GetSlotDirectionLabel(
	int32 SlotIndex,
	int32 InDirectionCount,
	float InAngleOffsetDegrees)
{
	static const TCHAR* Compass16[16] =
	{
		TEXT("N"), TEXT("NNE"), TEXT("NE"), TEXT("ENE"),
		TEXT("E"), TEXT("ESE"), TEXT("SE"), TEXT("SSE"),
		TEXT("S"), TEXT("SSW"), TEXT("SW"), TEXT("WSW"),
		TEXT("W"), TEXT("WNW"), TEXT("NW"), TEXT("NNW")
	};
	const bool bCompassFriendly = FMath::IsNearlyZero(InAngleOffsetDegrees)
		&& (InDirectionCount == 4 || InDirectionCount == 8 || InDirectionCount == 16)
		&& SlotIndex >= 0 && SlotIndex < InDirectionCount;
	return bCompassFriendly
		? FString(Compass16[SlotIndex * (16 / InDirectionCount)])
		: FString::FromInt(SlotIndex);
}

FText SDirectionalAnimationWheel::GetDynamicToolTipText() const
{
	if (CachedPresentation.IsValidIndex(HoveredSlotIndex))
	{
		const FCachedSegmentPresentation& Hovered = CachedPresentation[HoveredSlotIndex];
		const FText StateText = !Hovered.bValid
			? LOCTEXT("ToolTipInvalid", "Invalid — see Details for the reason")
			: (Hovered.bOccupied
				? Hovered.AssetLabel
				: LOCTEXT("ToolTipEmpty", "Empty — resolution fails here; no fallback"));
		return FText::Format(
			LOCTEXT("ToolTipSegmentFormat", "{0} · {1}° · {2}"),
			FText::FromString(GetSlotDirectionLabel(
				HoveredSlotIndex, GetDirectionCount(), GetAngleOffsetDegrees())),
			FText::FromString(FString::Printf(TEXT("%.0f"), GetSlotCenterBearingDegrees(
				HoveredSlotIndex, GetDirectionCount(), GetAngleOffsetDegrees()))),
			StateText);
	}
	return LOCTEXT(
		"WheelToolTip",
		"Point or click a direction. Arrow keys move clockwise or counterclockwise; "
		"Home and End jump; Enter or Space commits when direct commit is enabled; "
		"Escape cancels.");
}

FText SDirectionalAnimationWheel::GetAccessibleSummaryText() const
{
	if (!IsSupportedTopology())
	{
		return LOCTEXT(
			"AccessibleInvalidTopology",
			"Direction wheel unavailable. The direction count or angle offset is invalid.");
	}

	int32 OccupiedCount = 0;
	int32 InvalidCount = 0;
	for (const FCachedSegmentPresentation& Presentation : CachedPresentation)
	{
		OccupiedCount += Presentation.bOccupied ? 1 : 0;
		InvalidCount += Presentation.bValid ? 0 : 1;
	}
	const FText BaseText = CachedBaseAnimationText.IsEmpty()
		? LOCTEXT("AccessibleUnnamedBase", "unnamed base animation")
		: CachedBaseAnimationText;
	const FText SelectionText = CachedPresentation.IsValidIndex(CachedCurrentSelection)
		? FText::Format(
			LOCTEXT("AccessibleCurrentSelection", "Current selection is slot {0}."),
			FText::AsNumber(CachedCurrentSelection))
		: LOCTEXT("AccessibleNoCurrentSelection", "There is no current selection.");
	const FText CommitHelpText = IsDirectCommitEnabled()
		? LOCTEXT("AccessibleDirectCommitHelp", "Enter or Space commits.")
		: LOCTEXT(
			"AccessibleHeldCommitHelp",
			"Release the held direction-wheel shortcut to commit the pointed or keyboard-focused segment.");
	return FText::Format(
		LOCTEXT(
			"AccessibleSummary",
			"Direction wheel for {0}, {1} segments, {2} occupied and {3} invalid. {4} "
			"Tab visits each segment; arrow keys move between segments; Home and End jump; "
			"{5} Escape cancels."),
		BaseText,
		FText::AsNumber(GetDirectionCount()),
		FText::AsNumber(OccupiedCount),
		FText::AsNumber(InvalidCount),
		SelectionText,
		CommitHelpText);
}

FText SDirectionalAnimationWheel::GetAccessibleSegmentText(int32 SlotIndex) const
{
	if (!CachedPresentation.IsValidIndex(SlotIndex))
	{
		return FText::Format(
			LOCTEXT("AccessibleMissingSegment", "Slot {0}, unavailable."),
			FText::AsNumber(SlotIndex));
	}
	const FCachedSegmentPresentation& Presentation = CachedPresentation[SlotIndex];
	const FText OccupancyText = Presentation.bOccupied
		? LOCTEXT("AccessibleOccupied", "occupied")
		: LOCTEXT("AccessibleEmpty", "empty");
	const FText AssetText = Presentation.AssetLabel.IsEmpty()
		? LOCTEXT("AccessibleNoAsset", "no asset")
		: Presentation.AssetLabel;
	const FText SelectionText = SlotIndex == CachedCurrentSelection
		? LOCTEXT("AccessibleSelected", "selected")
		: (IsSegmentControlFocused(SlotIndex)
			? LOCTEXT("AccessibleFocused", "focused")
			: (SlotIndex == HoveredSlotIndex
				? LOCTEXT("AccessiblePointed", "pointed")
				: LOCTEXT("AccessibleNotSelected", "not selected")));
	const FText ValidationText = Presentation.bValid
		? LOCTEXT("AccessibleValid", "valid")
		: LOCTEXT("AccessibleInvalid", "invalid");
	const FString BearingText = FString::Printf(
		TEXT("%.1f"),
		GetSlotCenterBearingDegrees(SlotIndex, GetDirectionCount(), GetAngleOffsetDegrees()));
	return FText::Format(
		LOCTEXT(
			"AccessibleSegment",
			"Slot {0}, center {1} degrees clockwise from up, {2}, asset {3}, {4}, {5}."),
		FText::AsNumber(SlotIndex),
		FText::FromString(BearingText),
		OccupancyText,
		AssetText,
		SelectionText,
		ValidationText);
}

int32 SDirectionalAnimationWheel::GetDirectionCount() const
{
	return DirectionCount.Get(8);
}

float SDirectionalAnimationWheel::GetAngleOffsetDegrees() const
{
	return AngleOffsetDegrees.Get(0.0f);
}

bool SDirectionalAnimationWheel::IsDirectCommitEnabled() const
{
	return DirectCommitEnabled.Get(true);
}

bool SDirectionalAnimationWheel::IsSupportedTopology() const
{
	return UPaper2DPlusCharacterProfileAsset::AreDirectionalSettingsValid(
		GetDirectionCount(), GetAngleOffsetDegrees());
}

bool SDirectionalAnimationWheel::IsCommittableSlot(int32 SlotIndex) const
{
	return IsSupportedTopology()
		&& CachedPresentation.IsValidIndex(SlotIndex)
		&& CachedPresentation[SlotIndex].bValid;
}

bool SDirectionalAnimationWheel::IsSegmentControlFocused(int32 SlotIndex) const
{
	return SegmentControls.IsValidIndex(SlotIndex)
		&& SegmentControls[SlotIndex].IsValid()
		&& SegmentControls[SlotIndex]->HasKeyboardFocus();
}

int32 SDirectionalAnimationWheel::GetFocusedSegmentSlot() const
{
	for (int32 SlotIndex = 0; SlotIndex < SegmentControls.Num(); ++SlotIndex)
	{
		if (IsSegmentControlFocused(SlotIndex))
		{
			return SlotIndex;
		}
	}
	return INDEX_NONE;
}

int32 SDirectionalAnimationWheel::GetKeyboardAnchorSlot() const
{
	const int32 FocusedSlot = GetFocusedSegmentSlot();
	if (FocusedSlot != INDEX_NONE)
	{
		return FocusedSlot;
	}
	if (CachedPresentation.IsValidIndex(HoveredSlotIndex))
	{
		return HoveredSlotIndex;
	}
	if (CachedPresentation.IsValidIndex(CachedCurrentSelection))
	{
		return CachedCurrentSelection;
	}
	return IsSupportedTopology() ? 0 : INDEX_NONE;
}

bool SDirectionalAnimationWheel::CommitSlot(int32 SlotIndex)
{
	if (bInteractionResolved || !IsCommittableSlot(SlotIndex))
	{
		return false;
	}
	bInteractionResolved = true;
	OnSlotCommitted.ExecuteIfBound(SlotIndex);
	return true;
}

bool SDirectionalAnimationWheel::ResolveDirectPointerAction(int32 SlotIndex)
{
	if (!IsDirectCommitEnabled())
	{
		return false;
	}
	if (CommitSlot(SlotIndex))
	{
		return true;
	}
	if (!CachedPresentation.IsValidIndex(SlotIndex))
	{
		// Outside the ring the click expresses dismissal.
		CancelInteraction();
		return true;
	}
	// A real but non-committable wedge refuses in place: the wheel stays open with its warning
	// visible (the hub names the reason on hover) instead of silently eating the click.
	return false;
}

FReply SDirectionalAnimationWheel::MakeSegmentFocusReply(int32 SlotIndex)
{
	if (!SegmentControls.IsValidIndex(SlotIndex) || !SegmentControls[SlotIndex].IsValid())
	{
		return FReply::Handled();
	}
	return FReply::Handled().SetUserFocus(
		SegmentControls[SlotIndex].ToSharedRef(), EFocusCause::Navigation);
}

FReply SDirectionalAnimationWheel::HandleSegmentClicked(int32 SlotIndex)
{
	int32 CommitIndex = SlotIndex;
	if (SegmentControls.IsValidIndex(SlotIndex)
		&& SegmentControls[SlotIndex].IsValid()
		&& SegmentControls[SlotIndex]->IsHovered()
		&& FSlateApplication::IsInitialized())
	{
		const FGeometry& WheelGeometry = GetCachedGeometry();
		CommitIndex = HitTestSegment(
			WheelGeometry.AbsoluteToLocal(FSlateApplication::Get().GetCursorPos()),
			WheelGeometry.GetLocalSize(),
			GetDirectionCount(),
			GetAngleOffsetDegrees());
	}

	SetHoveredSlot(CommitIndex);
	if (ResolveDirectPointerAction(CommitIndex))
	{
		// Commit and cancellation can synchronously dismiss the menu; do not focus a child after it
		// has detached.
		return FReply::Handled();
	}
	FReply Reply = FReply::Handled();
	if (SegmentControls.IsValidIndex(CommitIndex) && SegmentControls[CommitIndex].IsValid())
	{
		Reply.SetUserFocus(SegmentControls[CommitIndex].ToSharedRef(), EFocusCause::Mouse);
	}
	return Reply;
}

void SDirectionalAnimationWheel::HandleSegmentHovered(int32 SlotIndex)
{
	SetHoveredSlot(SlotIndex);
}

void SDirectionalAnimationWheel::HandleSegmentUnhovered(int32 SlotIndex)
{
	if (HoveredSlotIndex == SlotIndex)
	{
		SetHoveredSlot(INDEX_NONE);
	}
}

void SDirectionalAnimationWheel::SetHoveredSlot(int32 SlotIndex)
{
	const int32 NewHoveredSlot = CachedPresentation.IsValidIndex(SlotIndex)
		? SlotIndex
		: INDEX_NONE;
	if (NewHoveredSlot == HoveredSlotIndex)
	{
		return;
	}
	HoveredSlotIndex = NewHoveredSlot;
	Invalidate(EInvalidateWidgetReason::Paint);
	OnHoveredSlotChanged.ExecuteIfBound(HoveredSlotIndex);
	NotifyAccessibleStateChanged();
}

bool SDirectionalAnimationWheel::RefreshCachedPresentation()
{
	TArray<FCachedSegmentPresentation> NewPresentation;
	const int32 InDirectionCount = GetDirectionCount();
	if (UPaper2DPlusCharacterProfileAsset::AreDirectionalSettingsValid(
		InDirectionCount, GetAngleOffsetDegrees()))
	{
		NewPresentation.SetNum(InDirectionCount);
		if (SegmentStates.IsSet())
		{
			for (FCachedSegmentPresentation& Presentation : NewPresentation)
			{
				Presentation.bValid = false;
			}
		}
		static const TArray<FPaper2DPlusDirectionalWheelSegment> EmptyStates;
		const TArray<FPaper2DPlusDirectionalWheelSegment>& SuppliedStates =
			SegmentStates.Get(EmptyStates);
		for (const FPaper2DPlusDirectionalWheelSegment& Supplied : SuppliedStates)
		{
			if (!NewPresentation.IsValidIndex(Supplied.SlotIndex))
			{
				continue;
			}
			FCachedSegmentPresentation& Target = NewPresentation[Supplied.SlotIndex];
			Target.bOccupied = Supplied.bOccupied;
			Target.bValid = Supplied.bValid;
			Target.AssetLabel = Supplied.AssetLabel;
		}
	}

	bool bChanged = NewPresentation.Num() != CachedPresentation.Num();
	if (!bChanged)
	{
		for (int32 Index = 0; Index < NewPresentation.Num(); ++Index)
		{
			const FCachedSegmentPresentation& NewState = NewPresentation[Index];
			const FCachedSegmentPresentation& CachedState = CachedPresentation[Index];
			if (NewState.bOccupied != CachedState.bOccupied
				|| NewState.bValid != CachedState.bValid
				|| !NewState.AssetLabel.EqualTo(CachedState.AssetLabel))
			{
				bChanged = true;
				break;
			}
		}
	}
	if (bChanged)
	{
		const bool bHoverBecameInvalid = !NewPresentation.IsValidIndex(HoveredSlotIndex);
		CachedPresentation = MoveTemp(NewPresentation);
		if (bHoverBecameInvalid && HoveredSlotIndex != INDEX_NONE)
		{
			HoveredSlotIndex = INDEX_NONE;
			OnHoveredSlotChanged.ExecuteIfBound(INDEX_NONE);
		}
	}
	return bChanged;
}

void SDirectionalAnimationWheel::RebuildCachedGeometry(const FVector2D& LocalSize)
{
	if (HoveredSlotIndex != INDEX_NONE)
	{
		HoveredSlotIndex = INDEX_NONE;
		OnHoveredSlotChanged.ExecuteIfBound(INDEX_NONE);
	}
	CachedGeometry.Reset();
	CachedCenterCircle.Reset();
	CachedLocalSize = LocalSize;
	CachedDirectionCount = GetDirectionCount();
	CachedAngleOffsetDegrees = GetAngleOffsetDegrees();
	// Built even for an unsupported topology: the plate still grounds the on-plate error message.
	// Radius must cover the north tick (OuterRadius + 7).
	if (LocalSize.X > 0.0 && LocalSize.Y > 0.0)
	{
		const double PlateRadius = FMath::Min(LocalSize.X, LocalSize.Y)
			* Paper2DPlusDirectionalWheelPrivate::OuterRadiusFraction + 8.0;
		PlateBrush = MakeUnique<FSlateRoundedBoxBrush>(
			FStyleColors::Recessed,
			static_cast<float>(PlateRadius),
			FVector2D(PlateRadius * 2.0, PlateRadius * 2.0));
	}
	if (!IsSupportedTopology() || LocalSize.X <= 0.0 || LocalSize.Y <= 0.0)
	{
		if (SegmentControls.Num() != CachedGeometry.Num())
		{
			RebuildSegmentControls();
		}
		return;
	}

	const FVector2D Center = LocalSize * 0.5;
	const double MinDimension = FMath::Min(LocalSize.X, LocalSize.Y);
	const double InnerRadius = MinDimension * Paper2DPlusDirectionalWheelPrivate::InnerRadiusFraction;
	const double OuterRadius = MinDimension * Paper2DPlusDirectionalWheelPrivate::OuterRadiusFraction;
	const double LabelRadius = (InnerRadius + OuterRadius) * 0.5;
	const double MarkerRadius = OuterRadius - 8.0;
	const double Separation = 360.0 / static_cast<double>(CachedDirectionCount);

	CachedGeometry.Reserve(CachedDirectionCount);
	for (int32 SlotIndex = 0; SlotIndex < CachedDirectionCount; ++SlotIndex)
	{
		FCachedSegmentGeometry& Segment = CachedGeometry.AddDefaulted_GetRef();
		Segment.SlotIndex = SlotIndex;
		Segment.CenterBearingDegrees = GetSlotCenterBearingDegrees(
			SlotIndex, CachedDirectionCount, CachedAngleOffsetDegrees);
		Segment.LabelCenter = Center
			+ Paper2DPlusDirectionalWheelPrivate::ScreenUnitFromBearing(
				Segment.CenterBearingDegrees) * LabelRadius;
		Segment.OuterMarkerCenter = Center
			+ Paper2DPlusDirectionalWheelPrivate::ScreenUnitFromBearing(
				Segment.CenterBearingDegrees) * MarkerRadius;
		Segment.SlotLabel = GetSlotDirectionLabel(
			SlotIndex, CachedDirectionCount, CachedAngleOffsetDegrees);
		Segment.LabelSize = Paper2DPlusDirectionalWheelPrivate::MeasureLabel(
			Segment.SlotLabel, Paper2DPlusDirectionalWheelPrivate::SlotLabelFont());
		const FVector2D IconCenter = Segment.LabelCenter
			- FVector2D(Segment.LabelSize.X * 0.5 + 7.0, 0.0);
		const FVector2D IconSize(9.0, 9.0);
		Segment.IconPosition = IconCenter - IconSize * 0.5;
		Segment.InvalidSlashA =
		{
			Segment.IconPosition,
			Segment.IconPosition + IconSize
		};
		Segment.InvalidSlashB =
		{
			Segment.IconPosition + FVector2D(IconSize.X, 0.0),
			Segment.IconPosition + FVector2D(0.0, IconSize.Y)
		};
		Segment.EmptyDiamond =
		{
			IconCenter + FVector2D(0.0, -5.0),
			IconCenter + FVector2D(5.0, 0.0),
			IconCenter + FVector2D(0.0, 5.0),
			IconCenter + FVector2D(-5.0, 0.0),
			IconCenter + FVector2D(0.0, -5.0)
		};
		Segment.SelectionMarker =
		{
			Segment.OuterMarkerCenter + FVector2D(0.0, -5.0),
			Segment.OuterMarkerCenter + FVector2D(5.0, 5.0),
			Segment.OuterMarkerCenter + FVector2D(-5.0, 5.0),
			Segment.OuterMarkerCenter + FVector2D(0.0, -5.0)
		};

		const double StartBearing = Segment.CenterBearingDegrees - Separation * 0.5;
		const double EndBearing = Segment.CenterBearingDegrees + Separation * 0.5;
		// Chord count follows arc length so a 3-way wheel's arcs stay flush with the 32-step
		// center circle instead of dipping visibly inside it.
		const int32 ArcSteps =
			Paper2DPlusDirectionalWheelPrivate::ArcStepsForSeparation(Separation);
		Segment.OutlinePoints.Reserve((ArcSteps + 1) * 2 + 1);
		for (int32 Step = 0; Step <= ArcSteps; ++Step)
		{
			const double Alpha = static_cast<double>(Step) / ArcSteps;
			const double Bearing = FMath::Lerp(StartBearing, EndBearing, Alpha);
			Segment.OutlinePoints.Add(
				Center
				+ Paper2DPlusDirectionalWheelPrivate::ScreenUnitFromBearing(Bearing) * OuterRadius);
		}
		for (int32 Step = ArcSteps; Step >= 0; --Step)
		{
			const double Alpha = static_cast<double>(Step) / ArcSteps;
			const double Bearing = FMath::Lerp(StartBearing, EndBearing, Alpha);
			Segment.OutlinePoints.Add(
				Center
				+ Paper2DPlusDirectionalWheelPrivate::ScreenUnitFromBearing(Bearing) * InnerRadius);
		}
		const FVector2D FirstOutlinePoint = Segment.OutlinePoints[0];
		Segment.OutlinePoints.Add(FirstOutlinePoint);

		// Filled wedge body: a mid-radius arc stroked at band thickness, inset a hair on each end
		// so adjacent fills read as separated wedges instead of overlapping at the spokes.
		const double MidRadius = (InnerRadius + OuterRadius) * 0.5;
		const double EndInsetDegrees =
			FMath::RadiansToDegrees(2.0 / FMath::Max(MidRadius, 1.0));
		Segment.FillThickness =
			static_cast<float>(FMath::Max(OuterRadius - InnerRadius - 3.0, 1.0));
		Segment.FillPoints = Paper2DPlusDirectionalWheelPrivate::BuildArcPoints(
			Center, MidRadius, StartBearing, EndBearing, EndInsetDegrees);
	}

	constexpr int32 CircleSteps = 32;
	CachedCenterCircle.Reserve(CircleSteps + 1);
	for (int32 Step = 0; Step <= CircleSteps; ++Step)
	{
		const double Bearing = 360.0 * static_cast<double>(Step) / CircleSteps;
		CachedCenterCircle.Add(
			Center
			+ Paper2DPlusDirectionalWheelPrivate::ScreenUnitFromBearing(Bearing) * InnerRadius);
	}

	if (SegmentControls.Num() != CachedGeometry.Num())
	{
		RebuildSegmentControls();
	}
	else if (SegmentCanvas.IsValid())
	{
		SegmentCanvas->Invalidate(EInvalidateWidgetReason::Layout);
	}
}

void SDirectionalAnimationWheel::RebuildSegmentControls()
{
	SegmentControls.Reset();
	if (!SegmentCanvas.IsValid())
	{
		return;
	}
	SegmentCanvas->ClearChildren();
	SegmentControls.Reserve(CachedGeometry.Num());

	for (const FCachedSegmentGeometry& Segment : CachedGeometry)
	{
		const int32 SlotIndex = Segment.SlotIndex;
		TSharedPtr<SButton> SegmentControl;
		SegmentCanvas->AddSlot()
			.Offset(TAttribute<FMargin>::CreateSP(
				this, &SDirectionalAnimationWheel::GetSegmentControlOffset, SlotIndex))
			.Anchors(FAnchors(0.0f, 0.0f))
			.Alignment(FVector2D::ZeroVector)
			.AutoSize(false)
			.ZOrder(1.0f)
			[
				SAssignNew(SegmentControl, SButton)
				.ButtonStyle(&FCoreStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("NoBorder")))
				.ButtonColorAndOpacity(FLinearColor::Transparent)
				.ContentPadding(0.0f)
				.ClickMethod(EButtonClickMethod::MouseDown)
				.IsFocusable(true)
				.AccessibleText(TAttribute<FText>::CreateSP(
					this, &SDirectionalAnimationWheel::GetAccessibleSegmentText, SlotIndex))
				.ToolTipText(TAttribute<FText>::CreateSP(
					this, &SDirectionalAnimationWheel::GetAccessibleSegmentText, SlotIndex))
				.OnHovered(FSimpleDelegate::CreateSP(
					this, &SDirectionalAnimationWheel::HandleSegmentHovered, SlotIndex))
				.OnUnhovered(FSimpleDelegate::CreateSP(
					this, &SDirectionalAnimationWheel::HandleSegmentUnhovered, SlotIndex))
				.OnClicked(FOnClicked::CreateSP(
					this, &SDirectionalAnimationWheel::HandleSegmentClicked, SlotIndex))
			];
		SegmentControls.Add(SegmentControl);
	}
}

FMargin SDirectionalAnimationWheel::GetSegmentControlOffset(int32 SlotIndex) const
{
	if (!CachedGeometry.IsValidIndex(SlotIndex)
		|| CachedGeometry[SlotIndex].SlotIndex != SlotIndex)
	{
		return FMargin(0.0f);
	}
	const float HalfSize = Paper2DPlusDirectionalWheelPrivate::SegmentControlSize * 0.5f;
	const FVector2D Position = CachedGeometry[SlotIndex].LabelCenter - FVector2D(HalfSize);
	return FMargin(
		static_cast<float>(Position.X),
		static_cast<float>(Position.Y),
		Paper2DPlusDirectionalWheelPrivate::SegmentControlSize,
		Paper2DPlusDirectionalWheelPrivate::SegmentControlSize);
}

void SDirectionalAnimationWheel::NotifyAccessibleStateChanged()
{
#if WITH_ACCESSIBILITY
	if (!FSlateApplication::IsInitialized() || !HasAnyUserFocusOrFocusedDescendants())
	{
		return;
	}
	const int32 FocusedSlot = GetFocusedSegmentSlot();
	const FString Announcement = (FocusedSlot != INDEX_NONE
		? GetAccessibleSegmentText(FocusedSlot)
		: GetAccessibleSummaryText()).ToString();
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
	FSlateApplication::Get().GetAccessibleMessageHandler()->OnWidgetEventRaised(
		SharedThis(this),
		EAccessibleEvent::Notification,
		FString(),
		Announcement);
#else
	FSlateApplication::Get().GetAccessibleMessageHandler()->OnWidgetEventRaised(
		FSlateAccessibleMessageHandler::FSlateWidgetAccessibleEventArgs(
			SharedThis(this),
			EAccessibleEvent::Notification,
			FString(),
			Announcement));
#endif
#endif
}

double SDirectionalAnimationWheel::GetSlotCenterBearingDegrees(
	int32 SlotIndex,
	int32 InDirectionCount,
	float InAngleOffsetDegrees)
{
	if (InDirectionCount < UPaper2DPlusCharacterProfileAsset::MinimumDirectionalCount
		|| InDirectionCount > UPaper2DPlusCharacterProfileAsset::MaximumDirectionalCount
		|| !FMath::IsFinite(InAngleOffsetDegrees)
		|| SlotIndex < 0
		|| SlotIndex >= InDirectionCount)
	{
		return 0.0;
	}
	const double Separation = 360.0 / static_cast<double>(InDirectionCount);
	return Paper2DPlusDirectionalWheelPrivate::NormalizeBearing(
		static_cast<double>(SlotIndex) * Separation - InAngleOffsetDegrees);
}

#undef LOCTEXT_NAMESPACE
