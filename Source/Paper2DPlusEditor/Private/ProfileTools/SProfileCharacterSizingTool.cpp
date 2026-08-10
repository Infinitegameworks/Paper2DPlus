// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "ProfileTools/SProfileCharacterSizingTool.h"

#include "Components/CapsuleComponent.h"
#include "EditorCanvasUtils.h"
#include "GameFramework/Actor.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusCharacterProfileComponent.h"
#include "PaperFlipbook.h"
#include "PaperSprite.h"
#include "ProfilePropertyRow.h"
#include "Rendering/DrawElements.h"
#include "ScopedTransaction.h"
#include "SpriteExtractionUtils.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "Paper2DPlusProfileTools"

namespace
{
	/** The reference pose's sprite for measuring and drawing: key frame 0 of its flipbook. */
	UPaperSprite* Sizing_ResolveReferenceSprite(
		const UPaper2DPlusCharacterProfileAsset* Asset,
		int32 EntryIndex)
	{
		if (!Asset || !Asset->Flipbooks.IsValidIndex(EntryIndex))
		{
			return nullptr;
		}
		UPaperFlipbook* Flipbook = Asset->Flipbooks[EntryIndex].Identity.Flipbook.Get();
		if (!Flipbook || Flipbook->GetNumKeyFrames() == 0)
		{
			return nullptr;
		}
		return Flipbook->GetKeyFrameChecked(0).Sprite;
	}
}

// =============================================================================
// Canvas
// =============================================================================

namespace
{
	/** Corner-handle half-size in canvas pixels. */
	constexpr float Sizing_HandleHalf = 6.0f;

	/** Rounding thresholds. Movement below these opens no transaction and writes nothing, so a
	 *  click, a twitch, or a sub-pixel drag never dirties the asset. */
	constexpr float Sizing_LocationStepUU = 0.1f;
	constexpr float Sizing_ScaleStep = 0.001f;

	float Sizing_Quantize(float Value, float Step)
	{
		return FMath::RoundToFloat(Value / Step) * Step;
	}
}

void SCharacterSizingCanvas::Construct(const FArguments& InArgs)
{
	Profile = InArgs._Profile;
	ReferenceEntryIndex = InArgs._ReferenceEntryIndex;
	TargetHeightUU = InArgs._TargetHeightUU;
	CapsuleRadiusUU = InArgs._CapsuleRadiusUU;
	OnGestureBegin = InArgs._OnGestureBegin;
	OnApply = InArgs._OnApply;
	OnGestureEnd = InArgs._OnGestureEnd;
	OnGestureCancel = InArgs._OnGestureCancel;
}

SCharacterSizingCanvas::~SCharacterSizingCanvas()
{
	// Widget destruction is what window close reduces to, and it is a boundary that invalidates the
	// gesture like any other. Settling here is what stops a transaction outliving its canvas.
	SettleGesture();
}

void SCharacterSizingCanvas::SettleGesture()
{
	if (DragMode == ECharacterSizingDragMode::None && !bNudgeActive)
	{
		return; // Idempotent: nothing live, nothing to close.
	}
	DragMode = ECharacterSizingDragMode::None;
	bNudgeActive = false;
	bHasWritten = false;
	OnGestureEnd.ExecuteIfBound();
}

bool SCharacterSizingCanvas::IsOverHandle(FVector2D LocalPos) const
{
	if (!bHasPaintedOnce || CachedSpriteRect.GetArea() <= 0.0f)
	{
		return false;
	}
	const FVector2D Corners[4] = {
		FVector2D(CachedSpriteRect.Left, CachedSpriteRect.Top),
		FVector2D(CachedSpriteRect.Right, CachedSpriteRect.Top),
		FVector2D(CachedSpriteRect.Left, CachedSpriteRect.Bottom),
		FVector2D(CachedSpriteRect.Right, CachedSpriteRect.Bottom),
	};
	for (const FVector2D& Corner : Corners)
	{
		if (FMath::Abs(LocalPos.X - Corner.X) <= Sizing_HandleHalf
			&& FMath::Abs(LocalPos.Y - Corner.Y) <= Sizing_HandleHalf)
		{
			return true;
		}
	}
	return false;
}

bool SCharacterSizingCanvas::IsOverSprite(FVector2D LocalPos) const
{
	return bHasPaintedOnce
		&& CachedSpriteRect.GetArea() > 0.0f
		&& CachedSpriteRect.ContainsPoint(LocalPos);
}

FReply SCharacterSizingCanvas::OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (Event.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}
	const UPaper2DPlusCharacterProfileAsset* Asset = Profile.Get();
	if (!Asset)
	{
		return FReply::Unhandled();
	}

	const FVector2D Local = Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition());

	// Split by TARGET, not by modifier: a corner handle scales, the body moves.
	ECharacterSizingDragMode Mode = ECharacterSizingDragMode::None;
	if (IsOverHandle(Local))
	{
		Mode = ECharacterSizingDragMode::Scale;
	}
	else if (IsOverSprite(Local))
	{
		Mode = ECharacterSizingDragMode::Move;
	}
	if (Mode == ECharacterSizingDragMode::None)
	{
		return FReply::Unhandled();
	}

	DragMode = Mode;
	DragStartLocal = Local;
	DragStartLocation = Asset->RelativeLocation;
	DragStartScale = Asset->RelativeScale3D;
	DragStartHandleDist = (Local - CachedSpriteRect.GetCenter()).Size();
	DragStartEntryIndex = ReferenceEntryIndex.Get(INDEX_NONE);
	// ARMED ONLY. No transaction, no write -- a click that never moves must leave the asset clean.
	bHasWritten = false;

	OnGestureBegin.ExecuteIfBound();
	return FReply::Handled().CaptureMouse(SharedThis(this)).SetUserFocus(SharedThis(this), EFocusCause::Mouse);
}

FReply SCharacterSizingCanvas::OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (DragMode == ECharacterSizingDragMode::None)
	{
		return FReply::Unhandled();
	}
	ApplyDragTo(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()));
	return FReply::Handled();
}

void SCharacterSizingCanvas::ApplyDragTo(FVector2D LocalPos)
{
	const UPaper2DPlusCharacterProfileAsset* Asset = Profile.Get();
	if (!Asset || DragMode == ECharacterSizingDragMode::None || CachedPixelsPerUU <= 0.0f)
	{
		return;
	}

	FVector NewLocation = DragStartLocation;
	FVector NewScale = DragStartScale;

	if (DragMode == ECharacterSizingDragMode::Move)
	{
		const FVector2D DeltaPx = LocalPos - DragStartLocal;
		// Screen Y is down, world Z is up.
		const float DeltaX = Sizing_Quantize(DeltaPx.X / CachedPixelsPerUU, Sizing_LocationStepUU);
		const float DeltaZ = Sizing_Quantize(-DeltaPx.Y / CachedPixelsPerUU, Sizing_LocationStepUU);
		if (FMath::IsNearlyZero(DeltaX) && FMath::IsNearlyZero(DeltaZ))
		{
			return; // Below the rounding threshold: no write, so no transaction.
		}
		NewLocation.X = DragStartLocation.X + DeltaX;
		NewLocation.Z = DragStartLocation.Z + DeltaZ;
	}
	else
	{
		if (DragStartHandleDist <= KINDA_SMALL_NUMBER)
		{
			return; // Grabbed the centre; a ratio would divide by ~zero.
		}
		const float Dist = (LocalPos - CachedSpriteRect.GetCenter()).Size();
		const float Ratio = Dist / DragStartHandleDist;
		// UNIFORM, so the character is never stretched by dragging a corner.
		const float Base = FMath::Abs(DragStartScale.Z) > KINDA_SMALL_NUMBER ? DragStartScale.Z : 1.0f;
		const float Scaled = Sizing_Quantize(Base * Ratio, Sizing_ScaleStep);
		if (FMath::IsNearlyEqual(Scaled, (float)DragStartScale.Z, Sizing_ScaleStep * 0.5f))
		{
			return;
		}
		if (FMath::Abs(Scaled) < Sizing_ScaleStep)
		{
			return; // Never collapse the character to nothing.
		}
		NewScale.X = Scaled;
		NewScale.Z = Scaled;
	}

	bHasWritten = true;
	OnApply.ExecuteIfBound(NewLocation, NewScale);
}

FReply SCharacterSizingCanvas::OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (Event.GetEffectingButton() != EKeys::LeftMouseButton
		|| DragMode == ECharacterSizingDragMode::None)
	{
		return FReply::Unhandled();
	}
	SettleGesture();
	return FReply::Handled().ReleaseMouseCapture();
}

void SCharacterSizingCanvas::OnMouseCaptureLost(const FCaptureLostEvent& Event)
{
	// Capture can be taken away without a button-up (a modal dialog, a focus steal). Same boundary.
	SettleGesture();
}

FReply SCharacterSizingCanvas::OnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
	// Esc cancels an in-flight drag and restores the pre-drag transform. NEW behaviour: no other
	// placement drag in this plugin offers it.
	if (Event.GetKey() == EKeys::Escape && DragMode != ECharacterSizingDragMode::None)
	{
		DragMode = ECharacterSizingDragMode::None;
		bHasWritten = false;
		OnGestureCancel.ExecuteIfBound();
		return FReply::Handled().ReleaseMouseCapture();
	}

	const float Step = Event.IsShiftDown() ? 10.0f : 1.0f;
	FVector2D Delta = FVector2D::ZeroVector;
	if (Event.GetKey() == EKeys::Left)        { Delta.X = -Step; }
	else if (Event.GetKey() == EKeys::Right)  { Delta.X = Step; }
	else if (Event.GetKey() == EKeys::Up)     { Delta.Y = Step; }
	else if (Event.GetKey() == EKeys::Down)   { Delta.Y = -Step; }
	else { return FReply::Unhandled(); }

	NudgeBy(Delta);
	return FReply::Handled();
}

void SCharacterSizingCanvas::NudgeBy(FVector2D DeltaUU)
{
	const UPaper2DPlusCharacterProfileAsset* Asset = Profile.Get();
	if (!Asset)
	{
		return;
	}

	// A key-repeat run must produce ONE undo entry, not one per key event, so the gesture opens on
	// the first repeat and stays open until key-up settles it.
	if (!bNudgeActive)
	{
		bNudgeActive = true;
		DragStartLocation = Asset->RelativeLocation;
		DragStartScale = Asset->RelativeScale3D;
		DragStartEntryIndex = ReferenceEntryIndex.Get(INDEX_NONE);
		bHasWritten = false;
		OnGestureBegin.ExecuteIfBound();
	}

	FVector NewLocation = Asset->RelativeLocation;
	NewLocation.X += DeltaUU.X;
	NewLocation.Z += DeltaUU.Y;
	bHasWritten = true;
	OnApply.ExecuteIfBound(NewLocation, Asset->RelativeScale3D);
}

FReply SCharacterSizingCanvas::OnKeyUp(const FGeometry& Geometry, const FKeyEvent& Event)
{
	if (!bNudgeActive)
	{
		return FReply::Unhandled();
	}
	const FKey Key = Event.GetKey();
	if (Key == EKeys::Left || Key == EKeys::Right || Key == EKeys::Up || Key == EKeys::Down)
	{
		SettleGesture();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

void SCharacterSizingCanvas::Tick(
	const FGeometry& Geometry,
	const double CurrentTime,
	const float DeltaTime)
{
	// Changing the previewed animation mid-gesture invalidates it: the deltas were measured against
	// a pose that is no longer drawn, and the sprite rect they hit-tested against is gone. Settle
	// BEFORE the state change takes effect rather than writing into the new target.
	if ((DragMode != ECharacterSizingDragMode::None || bNudgeActive)
		&& ReferenceEntryIndex.Get(INDEX_NONE) != DragStartEntryIndex)
	{
		SettleGesture();
	}
}

FCursorReply SCharacterSizingCanvas::OnCursorQuery(
	const FGeometry& Geometry,
	const FPointerEvent& Event) const
{
	const FVector2D Local = Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition());
	if (IsOverHandle(Local))
	{
		return FCursorReply::Cursor(EMouseCursor::ResizeSouthEast);
	}
	if (IsOverSprite(Local))
	{
		return FCursorReply::Cursor(EMouseCursor::CardinalCross);
	}
	return FCursorReply::Unhandled();
}

#if WITH_DEV_AUTOMATION_TESTS
void SCharacterSizingCanvas::SeedViewStateForTests(
	FVector2D InOrigin,
	float InPixelsPerUU,
	FSlateRect InSpriteRect)
{
	CachedOrigin = InOrigin;
	CachedPixelsPerUU = InPixelsPerUU;
	CachedSpriteRect = InSpriteRect;
	bHasPaintedOnce = true;
}

void SCharacterSizingCanvas::BeginGestureForTests(ECharacterSizingDragMode Mode, FVector2D LocalPos)
{
	const UPaper2DPlusCharacterProfileAsset* Asset = Profile.Get();
	if (!Asset)
	{
		return;
	}
	DragMode = Mode;
	DragStartLocal = LocalPos;
	DragStartLocation = Asset->RelativeLocation;
	DragStartScale = Asset->RelativeScale3D;
	DragStartHandleDist = (LocalPos - CachedSpriteRect.GetCenter()).Size();
	DragStartEntryIndex = ReferenceEntryIndex.Get(INDEX_NONE);
	bHasWritten = false;
	OnGestureBegin.ExecuteIfBound();
}

void SCharacterSizingCanvas::DragToForTests(FVector2D LocalPos)
{
	ApplyDragTo(LocalPos);
}

void SCharacterSizingCanvas::CancelGestureForTests()
{
	if (DragMode == ECharacterSizingDragMode::None)
	{
		return;
	}
	DragMode = ECharacterSizingDragMode::None;
	bHasWritten = false;
	OnGestureCancel.ExecuteIfBound();
}
#endif

int32 SCharacterSizingCanvas::OnPaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	bool bParentEnabled) const
{
	const FVector2D Size = AllottedGeometry.GetLocalSize();
	if (Size.X <= 1.0f || Size.Y <= 1.0f)
	{
		return LayerId;
	}

	FEditorCanvasUtils::DrawCheckerboard(OutDrawElements, LayerId, AllottedGeometry, 16.0f);
	int32 Layer = LayerId + 1;

	const UPaper2DPlusCharacterProfileAsset* Asset = Profile.Get();
	const float TargetHeight = TargetHeightUU.Get(0.0f);

	// With nothing configured, draw a documented placeholder rather than a degenerate capsule.
	const bool bPlaceholder = TargetHeight <= 0.0f;
	const float DrawHeight = bPlaceholder ? 180.0f : TargetHeight;
	const float DrawRadius = FMath::Max(CapsuleRadiusUU.Get(0.0f), DrawHeight * 0.18f);

	// View mapping: the capsule occupies most of the canvas with margin above and below.
	const float ViewHeightUU = DrawHeight * 1.8f;
	const float PixelsPerUU = Size.Y / FMath::Max(ViewHeightUU, 1.0f);
	const FVector2D Origin(Size.X * 0.5f, Size.Y * 0.5f); // actor origin = capsule centre

	// Cache the view state the canvas is ACTUALLY painting with, so hit testing shares one transform
	// with the draw and the two cannot drift apart.
	CachedOrigin = Origin;
	CachedPixelsPerUU = PixelsPerUU;
	bHasPaintedOnce = true;
	auto WorldToScreen = [&Origin, PixelsPerUU](float WorldX, float WorldZ)
	{
		// World Z is up; screen Y is down.
		return FVector2D(Origin.X + WorldX * PixelsPerUU, Origin.Y - WorldZ * PixelsPerUU);
	};

	const float GroundZ = -0.5f * DrawHeight;
	const float TopZ = 0.5f * DrawHeight;

	// ---- The sprite, at the profile's authored relative transform -------------------------------
	if (Asset && !bPlaceholder)
	{
		const int32 EntryIndex = ReferenceEntryIndex.Get(INDEX_NONE);
		if (UPaperSprite* Sprite = Sizing_ResolveReferenceSprite(Asset, EntryIndex))
		{
			const float SpritePPU = FMath::Max(Sprite->GetPixelsPerUnrealUnit(), 0.001f);
			const float UniformScale = FMath::Abs(Asset->RelativeScale3D.Z);
			// source px -> world uu (/PPU * scale) -> screen px (* PixelsPerUU)
			const float Zoom = (1.0f / SpritePPU) * UniformScale * PixelsPerUU;
			const FVector2D Centre = WorldToScreen(
				(float)Asset->RelativeLocation.X,
				(float)Asset->RelativeLocation.Z);

			FVector2D DrawPos = FVector2D::ZeroVector;
			FVector2D DrawSize = FVector2D::ZeroVector;
			FEditorCanvasUtils::DrawFlipbookSprite(
				OutDrawElements,
				Layer,
				AllottedGeometry,
				Sprite,
				&Asset->Flipbooks[EntryIndex],
				0,
				Centre,
				Zoom,
				FLinearColor::White,
				&DrawPos,
				&DrawSize);
			++Layer;

			// The hit rect comes from the SAME draw call that painted the sprite, so a drag can
			// never grab somewhere the sprite is not.
			CachedSpriteRect = FSlateRect(
				DrawPos.X, DrawPos.Y, DrawPos.X + DrawSize.X, DrawPos.Y + DrawSize.Y);

			// Corner handles: the scale affordance. Without them, "drag a handle" is undiscoverable.
			const FLinearColor HandleColor(0.95f, 0.75f, 0.25f, 0.95f);
			const FVector2D Corners[4] = {
				FVector2D(CachedSpriteRect.Left, CachedSpriteRect.Top),
				FVector2D(CachedSpriteRect.Right, CachedSpriteRect.Top),
				FVector2D(CachedSpriteRect.Left, CachedSpriteRect.Bottom),
				FVector2D(CachedSpriteRect.Right, CachedSpriteRect.Bottom),
			};
			for (const FVector2D& Corner : Corners)
			{
				FSlateDrawElement::MakeBox(
					OutDrawElements,
					Layer,
					AllottedGeometry.ToPaintGeometry(
						FVector2D(Sizing_HandleHalf * 2.0f, Sizing_HandleHalf * 2.0f),
						FSlateLayoutTransform(Corner - FVector2D(Sizing_HandleHalf, Sizing_HandleHalf))),
					FAppStyle::Get().GetBrush("WhiteBrush"),
					ESlateDrawEffect::None,
					HandleColor);
			}
			++Layer;
		}
		else
		{
			CachedSpriteRect = FSlateRect(0, 0, 0, 0);
		}
	}
	else
	{
		CachedSpriteRect = FSlateRect(0, 0, 0, 0);
	}

	// ---- Capsule outline (four edges plus two arcs approximated by segments) ---------------------
	const FLinearColor CapsuleColor = bPlaceholder
		? FLinearColor(0.55f, 0.55f, 0.55f, 0.55f)
		: FLinearColor(0.35f, 0.75f, 1.0f, 0.9f);

	{
		TArray<FVector2D> Left;
		TArray<FVector2D> Right;
		Left.Add(WorldToScreen(-DrawRadius, TopZ - DrawRadius));
		Left.Add(WorldToScreen(-DrawRadius, GroundZ + DrawRadius));
		Right.Add(WorldToScreen(DrawRadius, TopZ - DrawRadius));
		Right.Add(WorldToScreen(DrawRadius, GroundZ + DrawRadius));
		FSlateDrawElement::MakeLines(OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(), Left, ESlateDrawEffect::None, CapsuleColor, true, 1.0f);
		FSlateDrawElement::MakeLines(OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(), Right, ESlateDrawEffect::None, CapsuleColor, true, 1.0f);

		// Hemispherical caps.
		const int32 Segments = 16;
		TArray<FVector2D> TopArc;
		TArray<FVector2D> BottomArc;
		for (int32 i = 0; i <= Segments; ++i)
		{
			const float T = (float)i / (float)Segments;
			const float Angle = PI * T;
			const float X = -DrawRadius * FMath::Cos(Angle);
			const float ZOff = DrawRadius * FMath::Sin(Angle);
			TopArc.Add(WorldToScreen(X, TopZ - DrawRadius + ZOff));
			BottomArc.Add(WorldToScreen(X, GroundZ + DrawRadius - ZOff));
		}
		FSlateDrawElement::MakeLines(OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(), TopArc, ESlateDrawEffect::None, CapsuleColor, true, 1.0f);
		FSlateDrawElement::MakeLines(OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(), BottomArc, ESlateDrawEffect::None, CapsuleColor, true, 1.0f);
		++Layer;
	}

	// ---- Ground line ----------------------------------------------------------------------------
	{
		TArray<FVector2D> Ground;
		Ground.Add(FVector2D(0.0f, WorldToScreen(0.0f, GroundZ).Y));
		Ground.Add(FVector2D(Size.X, WorldToScreen(0.0f, GroundZ).Y));
		FSlateDrawElement::MakeLines(
			OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(), Ground,
			ESlateDrawEffect::None, FLinearColor(0.95f, 0.75f, 0.25f, 0.9f), true, 2.0f);
		++Layer;
	}

	return Layer;
}

// =============================================================================
// Tool
// =============================================================================

void SProfileCharacterSizingTool::Construct(const FArguments& InArgs)
{
	Profile = InArgs._Profile;

	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 6.0f)
		[
			FProfilePropertyRowUtils::MakeSectionTitle(LOCTEXT("SizingTitle", "Character Sizing"))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 8.0f)
		[
			FProfilePropertyRowUtils::MakeSectionHint(
				LOCTEXT("SizingHint",
					"Size the character against its gameplay capsule without leaving the editor. Fit computes a starting transform from the reference pose; the numbers below are the authored result."))
		]

		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.FillWidth(0.55f)
			[
				// MANDATORY: a pan/zoom canvas must be clipped by its host, or zoomed draws bleed
				// past the allocated geometry into the adjacent splitter slot.
				SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
				.Clipping(EWidgetClipping::ClipToBounds)
				.Padding(2.0f)
				[
					SAssignNew(Canvas, SCharacterSizingCanvas)
					.Profile(Profile)
					.ReferenceEntryIndex_Lambda([this]() { return ResolveReferenceEntryIndex(); })
					.TargetHeightUU_Lambda([this]() { return ResolveTargetHeightUU(); })
					.CapsuleRadiusUU_Lambda([this]() { return ResolveCapsuleRadiusUU(); })
					.OnGestureBegin(FOnCharacterSizingGestureBegin::CreateSP(
						this, &SProfileCharacterSizingTool::HandleGestureBegin))
					.OnApply(FOnCharacterSizingApply::CreateSP(
						this, &SProfileCharacterSizingTool::HandleGestureApply))
					.OnGestureEnd(FOnCharacterSizingGestureEnd::CreateSP(
						this, &SProfileCharacterSizingTool::HandleGestureEnd))
					.OnGestureCancel(FOnCharacterSizingGestureCancel::CreateSP(
						this, &SProfileCharacterSizingTool::HandleGestureCancel))
				]
			]

			+ SHorizontalBox::Slot()
			.FillWidth(0.45f)
			.Padding(8.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						FProfilePropertyRowUtils::MakeFactRow(
							LOCTEXT("SizingReferencePose", "Reference pose"),
							TAttribute<FText>::CreateLambda([this]()
							{
								const UPaper2DPlusCharacterProfileAsset* Asset = Profile.Get();
								const int32 Index = ResolveReferenceEntryIndex();
								return (Asset && Asset->Flipbooks.IsValidIndex(Index))
									? FText::FromString(Asset->Flipbooks[Index].Identity.FlipbookName)
									: LOCTEXT("SizingNoReference", "None");
							}),
							LOCTEXT("SizingReferencePoseTip",
								"Fitting against every animation's extents would let one outstretched pose shrink the idle, so the fit measures exactly this pose."))
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						FProfilePropertyRowUtils::MakeFactRow(
							LOCTEXT("SizingCapsuleHeight", "Capsule height"),
							TAttribute<FText>::CreateLambda([this]()
							{
								const float H = ResolveTargetHeightUU();
								return H > 0.0f
									? FText::Format(LOCTEXT("SizingUUFmt", "{0} uu"),
										FText::AsNumber(FMath::RoundToInt(H)))
									: LOCTEXT("SizingCapsuleUnset", "Not configured");
							}),
							LOCTEXT("SizingCapsuleTip",
								"Read from the configured character class's capsule on its class default object -- no spawned actor, no PIE. Falls back to the typed target height."))
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						FProfilePropertyRowUtils::MakeFactRow(
							LOCTEXT("SizingHeight", "Character height"),
							TAttribute<FText>::CreateSP(this, &SProfileCharacterSizingTool::GetHeightReadout),
							LOCTEXT("SizingHeightTip",
								"The height this character ends up at, in the same units as the capsule above."))
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 8.0f, 0.0f, 0.0f)
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
						.Text(LOCTEXT("SizingFit", "Fit to Capsule"))
						.ToolTipText(LOCTEXT("SizingFitTip",
							"Compute a starting transform from the reference pose, its pixels-per-unit, and the capsule. Uniform scale, feet on the ground line."))
						.IsEnabled_Lambda([this]() { return ResolveTargetHeightUU() > 0.0f; })
						.OnClicked_Lambda([this]() { ApplyFit(); return FReply::Handled(); })
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 6.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(this, &SProfileCharacterSizingTool::GetStatusText)
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
						.AutoWrapText(true)
					]
				]
			]
		]
	];
}

SProfileCharacterSizingTool::~SProfileCharacterSizingTool()
{
	// Last line of defence: a transaction must never outlive the panel that owns it.
	HandleGestureEnd();
}

void SProfileCharacterSizingTool::HandleGestureBegin()
{
	// ARM ONLY. No transaction here on purpose: a press that never moves must not produce an undo
	// entry, and opening one now would create one for every click on the sprite.
	const UPaper2DPlusCharacterProfileAsset* Asset = Profile.Get();
	if (!Asset)
	{
		return;
	}
	// Settle anything still live before re-arming, so two overlapping gestures cannot share one
	// transaction or strand it.
	HandleGestureEnd();
	GestureStartLocation = Asset->RelativeLocation;
	GestureStartScale = Asset->RelativeScale3D;
	bGestureLive = true;
}

void SProfileCharacterSizingTool::HandleGestureApply(FVector Location, FVector Scale)
{
	UPaper2DPlusCharacterProfileAsset* Asset = Profile.Get();
	if (!Asset || !bGestureLive)
	{
		return;
	}

	// LAZY OPEN: the first real write of the gesture opens the one transaction; every later delta
	// reuses it, so a whole drag collapses to a single undo entry.
	if (!ActiveTransaction.IsValid())
	{
		ActiveTransaction = MakeUnique<FScopedTransaction>(
			LOCTEXT("SizingDragTransaction", "Size Character"));
		Asset->Modify();
	}

	Asset->RelativeLocation = Location;
	Asset->RelativeScale3D = Scale;
	// No MarkPackageDirty here -- Modify() inside the transaction already dirties, and the readout
	// reads the asset live, so the drag stays visible as it happens.
}

void SProfileCharacterSizingTool::HandleGestureEnd()
{
	// THE settlement path. Idempotent, and safe when nothing is live or nothing was ever written.
	if (ActiveTransaction.IsValid())
	{
		ActiveTransaction.Reset();
		if (UPaper2DPlusCharacterProfileAsset* Asset = Profile.Get())
		{
			Asset->MarkPackageDirty();
		}
	}
	bGestureLive = false;
}

void SProfileCharacterSizingTool::HandleGestureCancel()
{
	UPaper2DPlusCharacterProfileAsset* Asset = Profile.Get();
	if (Asset && bGestureLive)
	{
		Asset->RelativeLocation = GestureStartLocation;
		Asset->RelativeScale3D = GestureStartScale;
	}
	if (ActiveTransaction.IsValid())
	{
		// Cancel rather than close: an aborted drag should leave no undo entry at all, not an entry
		// that undoes to the same place.
		ActiveTransaction->Cancel();
		ActiveTransaction.Reset();
	}
	bGestureLive = false;
}

float SProfileCharacterSizingTool::ResolveTargetHeightUU() const
{
#if WITH_EDITORONLY_DATA
	const UPaper2DPlusCharacterProfileAsset* Asset = Profile.Get();
	if (!Asset)
	{
		return 0.0f;
	}

	// Read the capsule off the CLASS DEFAULT OBJECT -- no spawned actor and no PIE, which is the
	// whole point of sizing in the editor.
	if (UClass* CharacterClass = Asset->SizingCharacterClass.Get())
	{
		if (const AActor* CDO = Cast<AActor>(CharacterClass->GetDefaultObject()))
		{
			if (const UCapsuleComponent* Capsule =
				CDO->FindComponentByClass<UCapsuleComponent>())
			{
				return Capsule->GetUnscaledCapsuleHalfHeight() * 2.0f;
			}
		}
	}
	return FMath::Max(Asset->SizingTargetHeight, 0.0f);
#else
	return 0.0f;
#endif
}

float SProfileCharacterSizingTool::ResolveCapsuleRadiusUU() const
{
#if WITH_EDITORONLY_DATA
	const UPaper2DPlusCharacterProfileAsset* Asset = Profile.Get();
	if (!Asset)
	{
		return 0.0f;
	}
	if (UClass* CharacterClass = Asset->SizingCharacterClass.Get())
	{
		if (const AActor* CDO = Cast<AActor>(CharacterClass->GetDefaultObject()))
		{
			if (const UCapsuleComponent* Capsule = CDO->FindComponentByClass<UCapsuleComponent>())
			{
				return Capsule->GetUnscaledCapsuleRadius();
			}
		}
	}
#endif
	return 0.0f;
}

int32 SProfileCharacterSizingTool::ResolveReferenceEntryIndex() const
{
	const UPaper2DPlusCharacterProfileAsset* Asset = Profile.Get();
	if (!Asset || Asset->Flipbooks.Num() == 0)
	{
		return INDEX_NONE;
	}

#if WITH_EDITORONLY_DATA
	if (!Asset->SizingReferenceAnimation.IsEmpty())
	{
		for (int32 i = 0; i < Asset->Flipbooks.Num(); ++i)
		{
			if (Asset->Flipbooks[i].Identity.FlipbookName.Equals(
				Asset->SizingReferenceAnimation, ESearchCase::IgnoreCase))
			{
				return i;
			}
		}
	}
#endif

	// Default to idle: the pose the character actually holds standing still.
	for (int32 i = 0; i < Asset->Flipbooks.Num(); ++i)
	{
		if (Asset->Flipbooks[i].Identity.FlipbookName.Contains(TEXT("Idle")))
		{
			return i;
		}
	}
	return 0;
}

bool SProfileCharacterSizingTool::BuildFitInput(Paper2DPlusCharacterSizing::FFitInput& Out) const
{
	const UPaper2DPlusCharacterProfileAsset* Asset = Profile.Get();
	const int32 EntryIndex = ResolveReferenceEntryIndex();
	UPaperSprite* Sprite = Sizing_ResolveReferenceSprite(Asset, EntryIndex);
	if (!Sprite)
	{
		return false;
	}

	Out.PixelsPerUnit = FMath::Max(Sprite->GetPixelsPerUnrealUnit(), 0.001f);
	Out.TargetHeightUU = ResolveTargetHeightUU();

	// Measure ONE sprite rather than running the whole-profile pixel scan: the fit only ever needs
	// the reference pose.
	const FVector2D SourceSize = FVector2D(Sprite->GetSourceSize());
	Out.SilhouetteWidthPx = (float)SourceSize.X;
	Out.SilhouetteHeightPx = (float)SourceSize.Y;

	// Pixel distance from the pivot DOWN to the bottom of the source region. This is what turns
	// "how tall" into "where", so feet land on the ground line instead of the sprite being centred.
	const FVector2D Pivot = Sprite->GetPivotPosition();
	const FVector2D SourceUV = FVector2D(Sprite->GetSourceUV());
	const float PivotLocalY = (float)(Pivot.Y - SourceUV.Y);
	Out.PivotToSilhouetteBottomPx = (float)SourceSize.Y - PivotLocalY;
	return true;
}

Paper2DPlusCharacterSizing::FFitResult SProfileCharacterSizingTool::ApplyFit()
{
	using namespace Paper2DPlusCharacterSizing;

	FFitResult Result;
	UPaper2DPlusCharacterProfileAsset* Asset = Profile.Get();
	if (!Asset)
	{
		Result.Reason = TEXT("No profile.");
		LastFitMessage = FText::FromString(Result.Reason);
		return Result;
	}

	FFitInput In;
	if (!BuildFitInput(In))
	{
		Result.Reason = TEXT("The reference pose has no sprite to measure.");
		LastFitMessage = FText::FromString(Result.Reason);
		return Result;
	}

	Result = ComputeFit(In);
	if (!Result.bValid)
	{
		// Fail closed: nothing is written, and the reason is shown rather than swallowed.
		LastFitMessage = FText::FromString(Result.Reason);
		return Result;
	}

	{
		FScopedTransaction Transaction(LOCTEXT("SizingFitTransaction", "Fit Character to Capsule"));
		Asset->Modify();
		Asset->RelativeLocation = Result.Location;
		Asset->RelativeScale3D = Result.Scale;
	}
	Asset->MarkPackageDirty();

	LastFitMessage = FText::Format(
		LOCTEXT("SizingFitDoneFmt", "Fitted to {0} uu ({1} m) at scale {2}."),
		FText::AsNumber(FMath::RoundToInt(Result.ResultingHeightUU)),
		FText::AsNumber(UnrealUnitsToMetres(Result.ResultingHeightUU)),
		FText::AsNumber(Result.Scale.Z));
	return Result;
}

float SProfileCharacterSizingTool::ResolveCurrentUniformScale() const
{
	const UPaper2DPlusCharacterProfileAsset* Asset = Profile.Get();
	return Asset ? (float)Asset->RelativeScale3D.Z : 1.0f;
}

FText SProfileCharacterSizingTool::GetHeightReadout() const
{
	using namespace Paper2DPlusCharacterSizing;

	FFitInput In;
	if (!BuildFitInput(In))
	{
		return LOCTEXT("SizingHeightUnknown", "—");
	}

	const float HeightUU = ComputeHeightUU(
		In.SilhouetteHeightPx, In.PixelsPerUnit, ResolveCurrentUniformScale());
	if (HeightUU <= 0.0f)
	{
		return LOCTEXT("SizingHeightUnknown", "—");
	}

	// Both units, because the capsule is authored in Unreal units and humans think in metres.
	return FText::Format(
		LOCTEXT("SizingHeightFmt", "{0} uu  ·  {1} m"),
		FText::AsNumber(FMath::RoundToInt(HeightUU)),
		FText::AsNumber(FMath::RoundToFloat(UnrealUnitsToMetres(HeightUU) * 100.0f) / 100.0f));
}

FText SProfileCharacterSizingTool::GetStatusText() const
{
	if (!LastFitMessage.IsEmpty())
	{
		return LastFitMessage;
	}
	if (ResolveTargetHeightUU() <= 0.0f)
	{
		// The unconfigured profile is the NORMAL first-run case, so it gets a prompt rather than a
		// degenerate capsule or a silent dead button.
		return LOCTEXT("SizingUnconfigured",
			"Set a character class with a capsule, or type a target height, to enable Fit. The preview shows a placeholder capsule until then.");
	}
	return LOCTEXT("SizingReady", "Press Fit to Capsule to compute a starting transform.");
}

#undef LOCTEXT_NAMESPACE
