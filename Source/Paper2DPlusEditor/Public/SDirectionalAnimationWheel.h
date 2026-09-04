// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Widgets/SCompoundWidget.h"

class SButton;
class SConstraintCanvas;

/** Presentation supplied by the host for one authored direction slot. */
struct PAPER2DPLUSEDITOR_API FPaper2DPlusDirectionalWheelSegment
{
	int32 SlotIndex = INDEX_NONE;
	bool bOccupied = false;
	bool bValid = true;
	FText AssetLabel;
};

/**
 * Reusable, mutation-free radial selector for a 3-16 direction topology.
 *
 * The widget owns only hover/focus interaction. Profile identity, popup/input-processor lifetime,
 * authoring transactions, and gameplay state remain with its host. Slot zero points up on screen
 * (+Y in directional-animation space) and indices advance clockwise.
 */
class PAPER2DPLUSEDITOR_API SDirectionalAnimationWheel final : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_OneParam(FOnHoveredSlotChanged, int32 /*SlotIndex or INDEX_NONE*/);
	DECLARE_DELEGATE_OneParam(FOnSlotCommitted, int32 /*SlotIndex*/);
	DECLARE_DELEGATE_OneParam(FOnCancelled, bool /*bRestoreFocus*/);

	SLATE_BEGIN_ARGS(SDirectionalAnimationWheel)
		: _DirectionCount(8)
		, _AngleOffsetDegrees(0.0f)
		, _CurrentSelection(INDEX_NONE)
		, _BaseAnimationText(FText::GetEmpty())
		, _DirectCommitEnabled(true)
	{}
		SLATE_ATTRIBUTE(int32, DirectionCount)
		SLATE_ATTRIBUTE(float, AngleOffsetDegrees)
		SLATE_ATTRIBUTE(TArray<FPaper2DPlusDirectionalWheelSegment>, SegmentStates)
		SLATE_ATTRIBUTE(int32, CurrentSelection)
		SLATE_ATTRIBUTE(FText, BaseAnimationText)
		/** Allow click and Enter/Space to commit. Held-shortcut hosts disable this and commit on key-up. */
		SLATE_ATTRIBUTE(bool, DirectCommitEnabled)
		SLATE_EVENT(FOnHoveredSlotChanged, OnHoveredSlotChanged)
		SLATE_EVENT(FOnSlotCommitted, OnSlotCommitted)
		SLATE_EVENT(FOnCancelled, OnCancelled)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
	virtual int32 OnPaint(
		const FPaintArgs& Args,
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;
	virtual void Tick(
		const FGeometry& AllottedGeometry,
		double InCurrentTime,
		float InDeltaTime) override;

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnFocusReceived(
		const FGeometry& MyGeometry,
		const FFocusEvent& InFocusEvent) override;
	virtual void OnFocusLost(const FFocusEvent& InFocusEvent) override;
	virtual void OnFocusChanging(
		const FWeakWidgetPath& PreviousFocusPath,
		const FWidgetPath& NewWidgetPath,
		const FFocusEvent& InFocusEvent) override;
	virtual FReply OnKeyDown(
		const FGeometry& MyGeometry,
		const FKeyEvent& InKeyEvent) override;
	virtual FReply OnMouseMove(
		const FGeometry& MyGeometry,
		const FPointerEvent& MouseEvent) override;
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDown(
		const FGeometry& MyGeometry,
		const FPointerEvent& MouseEvent) override;
	virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;

	/** Commit only the currently hovered valid slot. */
	bool CommitHoveredSlot();
	/** Held-key release treats any live pointer hover as authoritative, otherwise uses keyboard focus. */
	bool CommitHeldSelection();

	/** Signal cancellation once. Popup teardown and focus restoration remain host responsibilities. */
	void CancelInteraction(bool bRestoreFocus = true);
	/** Suppress late focus/capture callbacks once the host has taken ownership of teardown. */
	void ResolveForHostTeardown();

	int32 GetHoveredSlot() const { return HoveredSlotIndex; }
	FText GetAccessibleSummaryText() const;
	FText GetAccessibleSegmentText(int32 SlotIndex) const;

	/** Pure Slate-coordinate hit math used by pointer handling and headless automation. */
	static int32 HitTestSegment(
		const FVector2D& LocalPoint,
		const FVector2D& LocalSize,
		int32 DirectionCount,
		float AngleOffsetDegrees);

	/** Pure wrapped index traversal used by keyboard handling and headless automation. */
	static int32 TraverseSlotIndex(int32 CurrentIndex, int32 Delta, int32 DirectionCount);

	/**
	 * Designer-facing name for one slot: a compass label (N, NE, ...) for zero-offset 4/8/16-way
	 * topologies, otherwise the bare slot index. Shared by the wheel, the tool header, and the
	 * Details per-slot rows so every surface names a direction the same way.
	 */
	static FString GetSlotDirectionLabel(
		int32 SlotIndex,
		int32 DirectionCount,
		float AngleOffsetDegrees);

private:
	struct FCachedSegmentGeometry
	{
		int32 SlotIndex = INDEX_NONE;
		double CenterBearingDegrees = 0.0;
		FVector2D LabelCenter = FVector2D::ZeroVector;
		FVector2D OuterMarkerCenter = FVector2D::ZeroVector;
		FVector2D IconPosition = FVector2D::ZeroVector;
		FString SlotLabel;
		FVector2D LabelSize = FVector2D(10.0, 12.0);
		TArray<FVector2D> OutlinePoints;
		/** Mid-radius arc stroked at band thickness: the wedge's filled body. */
		TArray<FVector2D> FillPoints;
		float FillThickness = 0.0f;
		TArray<FVector2D> InvalidSlashA;
		TArray<FVector2D> InvalidSlashB;
		TArray<FVector2D> EmptyDiamond;
		TArray<FVector2D> SelectionMarker;
	};

	struct FCachedSegmentPresentation
	{
		bool bOccupied = false;
		bool bValid = true;
		FText AssetLabel;
	};

	int32 GetDirectionCount() const;
	float GetAngleOffsetDegrees() const;
	bool IsDirectCommitEnabled() const;
	FText GetDynamicToolTipText() const;
	bool IsSupportedTopology() const;
	bool IsCommittableSlot(int32 SlotIndex) const;
	bool IsSegmentControlFocused(int32 SlotIndex) const;
	int32 GetFocusedSegmentSlot() const;
	int32 GetKeyboardAnchorSlot() const;
	bool CommitSlot(int32 SlotIndex);
	/** Direct pointer actions commit valid slots and cancel every non-committable destination. */
	bool ResolveDirectPointerAction(int32 SlotIndex);
	FReply MakeSegmentFocusReply(int32 SlotIndex);
	FReply HandleSegmentClicked(int32 SlotIndex);
	void HandleSegmentHovered(int32 SlotIndex);
	void HandleSegmentUnhovered(int32 SlotIndex);
	void SetHoveredSlot(int32 SlotIndex);
	bool RefreshCachedPresentation();
	void RebuildCachedGeometry(const FVector2D& LocalSize);
	void RebuildSegmentControls();
	FMargin GetSegmentControlOffset(int32 SlotIndex) const;
	void NotifyAccessibleStateChanged();
	static double GetSlotCenterBearingDegrees(
		int32 SlotIndex,
		int32 DirectionCount,
		float AngleOffsetDegrees);

	TAttribute<int32> DirectionCount;
	TAttribute<float> AngleOffsetDegrees;
	TAttribute<TArray<FPaper2DPlusDirectionalWheelSegment>> SegmentStates;
	TAttribute<int32> CurrentSelection;
	TAttribute<FText> BaseAnimationText;
	TAttribute<bool> DirectCommitEnabled;
	FOnHoveredSlotChanged OnHoveredSlotChanged;
	FOnSlotCommitted OnSlotCommitted;
	FOnCancelled OnCancelled;
	TSharedPtr<SConstraintCanvas> SegmentCanvas;
	TArray<TSharedPtr<SButton>> SegmentControls;

	TArray<FCachedSegmentGeometry> CachedGeometry;
	TArray<FCachedSegmentPresentation> CachedPresentation;
	TArray<FVector2D> CachedCenterCircle;
	/** Theme-tracking round backing plate; rebuilt with the geometry so its radius follows size. */
	TUniquePtr<FSlateRoundedBoxBrush> PlateBrush;
	FVector2D CachedLocalSize = FVector2D::ZeroVector;
	int32 CachedDirectionCount = INDEX_NONE;
	float CachedAngleOffsetDegrees = 0.0f;
	int32 CachedCurrentSelection = INDEX_NONE;
	FText CachedBaseAnimationText;
	FString CachedPaintBaseAnimationLabel;
	int32 HoveredSlotIndex = INDEX_NONE;
	int32 CachedFocusedSegmentSlot = INDEX_NONE;
	bool bInteractionResolved = false;
	bool bCaptureLossPending = false;
	/** Focus-visible rule: mouse-caused focus never shows the standing focus ring. */
	EFocusCause LastFocusCause = EFocusCause::SetDirectly;
	FVector2D CachedCenterLabelSize = FVector2D::ZeroVector;
};
