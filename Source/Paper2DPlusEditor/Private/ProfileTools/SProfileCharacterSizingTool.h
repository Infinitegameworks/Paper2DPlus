// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CharacterSizingFit.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"

class FScopedTransaction;
class SVerticalBox;
class UPaper2DPlusCharacterProfileAsset;
class UPaperSprite;

/** What a live sizing gesture is doing. Split by TARGET, not by modifier key. */
enum class ECharacterSizingDragMode : uint8
{
	None,
	/** Dragging the sprite body: writes RelativeLocation. */
	Move,
	/** Dragging a corner handle on the sprite's bounding box: writes a UNIFORM RelativeScale3D. */
	Scale,
};

/** The canvas asks the panel to write these; the panel owns the transaction. */
DECLARE_DELEGATE(FOnCharacterSizingGestureBegin);
DECLARE_DELEGATE_TwoParams(FOnCharacterSizingApply, FVector /*Location*/, FVector /*Scale*/);
DECLARE_DELEGATE(FOnCharacterSizingGestureEnd);
DECLARE_DELEGATE(FOnCharacterSizingGestureCancel);

/**
 * Draws the reference pose against the gameplay capsule and ground line at the profile's authored
 * Relative Transform, and lets the designer drag it -- so sizing happens where the fit is visible
 * instead of in a character Blueprint that then gets transcribed back by hand.
 *
 * GESTURE CONTRACT (the Sprite tool's, which is the live precedent):
 *   - the CANVAS owns capture and coordinates; the PANEL owns the transaction and the mutation;
 *   - a press arms a latch but opens nothing -- a click with no movement must not dirty the asset;
 *   - the first non-zero ROUNDED delta lazily opens one transaction, and later deltas reuse it;
 *   - one idempotent settlement path closes it, called from every boundary that can invalidate the
 *     gesture: mouse up, lost capture, focus loss, reference-pose change, and widget destruction
 *     (which is what window close reduces to).
 *
 * Esc cancels and restores the pre-drag transform. That is NEW behaviour -- no placement drag in
 * this plugin has offered it before -- so it is stated rather than assumed.
 *
 * Holds no strong UObject reference across paints: it resolves the sprite from a weak profile
 * handle each paint, so it needs no GC root. A cached UObject* here is the documented
 * access-violation-inside-OnPaint crash.
 */
class SCharacterSizingCanvas : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SCharacterSizingCanvas) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset>, Profile)
		SLATE_ATTRIBUTE(int32, ReferenceEntryIndex)
		SLATE_ATTRIBUTE(float, TargetHeightUU)
		SLATE_ATTRIBUTE(float, CapsuleRadiusUU)
		SLATE_EVENT(FOnCharacterSizingGestureBegin, OnGestureBegin)
		SLATE_EVENT(FOnCharacterSizingApply, OnApply)
		SLATE_EVENT(FOnCharacterSizingGestureEnd, OnGestureEnd)
		SLATE_EVENT(FOnCharacterSizingGestureCancel, OnGestureCancel)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SCharacterSizingCanvas() override;

	virtual int32 OnPaint(
		const FPaintArgs& Args,
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(320.0f, 360.0f); }
	virtual bool SupportsKeyboardFocus() const override { return true; }

	virtual FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual void OnMouseCaptureLost(const FCaptureLostEvent& Event) override;
	virtual FReply OnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
	virtual FReply OnKeyUp(const FGeometry& Geometry, const FKeyEvent& Event) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& Geometry, const FPointerEvent& Event) const override;
	virtual void Tick(const FGeometry& Geometry, const double CurrentTime, const float DeltaTime) override;

	/** THE settlement path. Idempotent, and safe to call when no gesture is live. */
	void SettleGesture();

	ECharacterSizingDragMode GetDragMode() const { return DragMode; }

#if WITH_DEV_AUTOMATION_TESTS
	/** Drives the gesture without Slate input, using canvas-local pixel coordinates. */
	void BeginGestureForTests(ECharacterSizingDragMode Mode, FVector2D LocalPos);
	void DragToForTests(FVector2D LocalPos);
	void CancelGestureForTests();
	bool HasOpenedWriteForTests() const { return bHasWritten; }
	/** Seeds the paint-derived view state so hit testing and deltas work headlessly. */
	void SeedViewStateForTests(FVector2D InOrigin, float InPixelsPerUU, FSlateRect InSpriteRect);
#endif

private:
	void ApplyDragTo(FVector2D LocalPos);
	bool IsOverHandle(FVector2D LocalPos) const;
	bool IsOverSprite(FVector2D LocalPos) const;
	void NudgeBy(FVector2D DeltaUU);

	TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> Profile;
	TAttribute<int32> ReferenceEntryIndex;
	TAttribute<float> TargetHeightUU;
	TAttribute<float> CapsuleRadiusUU;

	FOnCharacterSizingGestureBegin OnGestureBegin;
	FOnCharacterSizingApply OnApply;
	FOnCharacterSizingGestureEnd OnGestureEnd;
	FOnCharacterSizingGestureCancel OnGestureCancel;

	// Paint-derived view state, cached so hit testing uses the SAME transform the canvas painted
	// with and cannot drift from it.
	mutable FVector2D CachedOrigin = FVector2D::ZeroVector;
	mutable float CachedPixelsPerUU = 1.0f;
	mutable FSlateRect CachedSpriteRect = FSlateRect(0, 0, 0, 0);
	mutable bool bHasPaintedOnce = false;

	ECharacterSizingDragMode DragMode = ECharacterSizingDragMode::None;
	FVector2D DragStartLocal = FVector2D::ZeroVector;
	FVector DragStartLocation = FVector::ZeroVector;
	FVector DragStartScale = FVector::OneVector;
	float DragStartHandleDist = 0.0f;
	/** The animation the gesture started against. If it changes mid-drag the gesture settles rather
	 *  than continuing to write deltas measured against a pose that is no longer on screen. */
	int32 DragStartEntryIndex = INDEX_NONE;
	/** True once a rounded delta actually produced a write, so a click never dirties the asset. */
	bool bHasWritten = false;
	bool bNudgeActive = false;
};

/**
 * The Character Sizing tool.
 *
 * Satisfies R6 (size inside the editor -- no Blueprint, no spawned actor, no PIE), R7 (direct
 * manipulation: drag the body to move, drag a corner handle to scale uniformly), R8 (a computed Fit
 * assist from the reference pose, measured silhouette, and capsule) and R9 (height legible in real
 * units while sizing, updating during the drag rather than only on release).
 */
class SProfileCharacterSizingTool : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SProfileCharacterSizingTool) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset>, Profile)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SProfileCharacterSizingTool() override;

	float ResolveTargetHeightUU() const;
	float ResolveCapsuleRadiusUU() const;
	int32 ResolveReferenceEntryIndex() const;

	bool BuildFitInput(Paper2DPlusCharacterSizing::FFitInput& Out) const;
	Paper2DPlusCharacterSizing::FFitResult ApplyFit();

	// ---- Gesture ownership: the panel holds the transaction, never the canvas ------------------
	void HandleGestureBegin();
	void HandleGestureApply(FVector Location, FVector Scale);
	/** Idempotent. Closing a gesture that never wrote closes no transaction. */
	void HandleGestureEnd();
	void HandleGestureCancel();

#if WITH_DEV_AUTOMATION_TESTS
	FText GetHeightReadoutForTests() const { return GetHeightReadout(); }
	FText GetStatusForTests() const { return GetStatusText(); }
	TSharedPtr<SCharacterSizingCanvas> GetCanvasForTests() const { return Canvas; }
	bool HasOpenTransactionForTests() const { return ActiveTransaction.IsValid(); }
#endif

private:
	FText GetHeightReadout() const;
	FText GetStatusText() const;
	float ResolveCurrentUniformScale() const;

	TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> Profile;
	TSharedPtr<SCharacterSizingCanvas> Canvas;
	FText LastFitMessage;

	/** Opened lazily on the first real write of a gesture; closed by the one settlement path. */
	TUniquePtr<FScopedTransaction> ActiveTransaction;
	FVector GestureStartLocation = FVector::ZeroVector;
	FVector GestureStartScale = FVector::OneVector;
	bool bGestureLive = false;
};
