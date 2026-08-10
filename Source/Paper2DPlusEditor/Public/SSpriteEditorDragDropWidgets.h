// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Input/DragAndDrop.h"
#include "UObject/WeakObjectPtr.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"

class UPaper2DPlusCharacterProfileAsset;

// ---------------------------------------------------------------------------
// Drag-drop operation for frame reordering
// ---------------------------------------------------------------------------
class FFrameReorderDragDropOp : public FDragDropOperation
{
public:
	DRAG_DROP_OPERATOR_TYPE(FFrameReorderDragDropOp, FDragDropOperation)

	int32 SourceFrameIndex = INDEX_NONE;

	static TSharedRef<FFrameReorderDragDropOp> New(int32 InSourceIndex)
	{
		TSharedRef<FFrameReorderDragDropOp> Op = MakeShareable(new FFrameReorderDragDropOp());
		Op->SourceFrameIndex = InSourceIndex;
		Op->Construct();
		return Op;
	}

	virtual TSharedPtr<SWidget> GetDefaultDecorator() const override
	{
		return SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.DarkGroupBorder"))
			.Padding(FMargin(6, 2))
			[
				SNew(STextBlock)
				.Text(FText::Format(NSLOCTEXT("SpriteEditorDragDrop", "DragFrameLabel", "Frame {0}"), FText::AsNumber(SourceFrameIndex)))
			];
	}
};

// ---------------------------------------------------------------------------
// Widget wrapper that enables drag-to-reorder on frame list entries
// ---------------------------------------------------------------------------
class SFrameDragDropWrapper : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SFrameDragDropWrapper) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	int32 FrameIndex = INDEX_NONE;
	TFunction<void(int32, const FPointerEvent&)> OnRightClickedFunc;
	TFunction<void(int32, int32)> OnFrameDroppedFunc;

	// Drag arming is owned EXTERNALLY (by the panel), not per-instance: selecting a
	// frame on mouse-down rebuilds the frame strip and destroys this wrapper mid-gesture,
	// so a local armed flag would be lost before OnMouseMove ever sees it. The panel keeps
	// the arm state alive across that rebuild; the freshly-built wrapper under the cursor
	// reads it back here. OnDragArmFunc records (source frame, start pos) in the tunnel
	// phase; OnDragDetectFunc returns a drag-drop op once the threshold is crossed (or null,
	// disarming itself when the left button is no longer held).
	TFunction<void(int32 /*FrameIndex*/, const FVector2D& /*ScreenPos*/)> OnDragArmFunc;
	TFunction<TSharedPtr<FDragDropOperation>(const FVector2D& /*ScreenPos*/, bool /*bLeftButtonDown*/)> OnDragDetectFunc;

	void Construct(const FArguments& InArgs)
	{
		ChildSlot
		[
			SNew(SVerticalBox)

			// Drop indicator line (visible only during drag-over)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SAssignNew(DropIndicator, SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
				.BorderBackgroundColor(FLinearColor(0.3f, 0.6f, 1.0f))
				.Padding(0)
				.Visibility(EVisibility::Collapsed)
				[
					SNew(SBox)
					.HeightOverride(2)
				]
			]

			// Actual content
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				InArgs._Content.Widget
			]
		];
	}

	// The wrapped frame-strip cell selects on left-mouse-DOWN and returns Handled (see
	// FFrameStripCellUtils::Build), consuming the bubble-phase event before our own
	// OnMouseButtonDown could run. Arm the drag here in the tunnel phase (root->leaf,
	// BEFORE the cell) and return Unhandled so the cell still performs its selection.
	// Arming is delegated to the panel (OnDragArmFunc) because the selection synchronously
	// rebuilds the strip and destroys THIS wrapper — only panel-owned state survives.
	virtual FReply OnPreviewMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && OnDragArmFunc)
		{
			OnDragArmFunc(FrameIndex, MouseEvent.GetScreenSpacePosition());
		}
		return FReply::Unhandled();
	}

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		// Left-down is armed in OnPreviewMouseButtonDown above (and the cell consumes the
		// bubble-phase left-down for selection). Only the right-click fallback remains here.
		if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
		{
			if (OnRightClickedFunc) { OnRightClickedFunc(FrameIndex, MouseEvent); }
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}

	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		// No mouse capture is taken (it would suppress the cell's selection-on-down), so
		// OnMouseMove bubbles to whichever wrapper is currently under the cursor — including
		// the one rebuilt by the selection refresh — ahead of any ancestor scroll box. The
		// panel owns the threshold decision and disarms itself when the left button is no
		// longer held (self-healing an arm left stale by a release that landed off-widget,
		// and preventing a button-less hover from starting a spurious drag).
		if (OnDragDetectFunc)
		{
			TSharedPtr<FDragDropOperation> Op = OnDragDetectFunc(
				MouseEvent.GetScreenSpacePosition(),
				MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton));
			if (Op.IsValid())
			{
				return FReply::Handled().BeginDragDrop(Op.ToSharedRef());
			}
		}
		return FReply::Unhandled();
	}

	virtual void OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override
	{
		if (DragDropEvent.GetOperationAs<FFrameReorderDragDropOp>().IsValid())
		{
			DropIndicator->SetVisibility(EVisibility::Visible);
		}
	}

	virtual void OnDragLeave(const FDragDropEvent& DragDropEvent) override
	{
		DropIndicator->SetVisibility(EVisibility::Collapsed);
	}

	virtual FReply OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override
	{
		if (DragDropEvent.GetOperationAs<FFrameReorderDragDropOp>().IsValid())
		{
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}

	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override
	{
		DropIndicator->SetVisibility(EVisibility::Collapsed);
		TSharedPtr<FFrameReorderDragDropOp> Op = DragDropEvent.GetOperationAs<FFrameReorderDragDropOp>();
		if (Op.IsValid() && Op->SourceFrameIndex != FrameIndex && OnFrameDroppedFunc)
		{
			OnFrameDroppedFunc(Op->SourceFrameIndex, FrameIndex);
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}

private:
	TSharedPtr<SBorder> DropIndicator;
};

// ---------------------------------------------------------------------------
// Unified drag-drop operation for queue interactions
// Handles both flipbook-list-to-queue drags and queue-reorder drags
// ---------------------------------------------------------------------------
class FQueueDragDropOp : public FDragDropOperation
{
public:
	DRAG_DROP_OPERATOR_TYPE(FQueueDragDropOp, FDragDropOperation)

	int32 FlipbookIndex = INDEX_NONE;    // Set for flipbook-list-to-queue drags (primary/single)
	TArray<int32> FlipbookIndices;        // All flipbook indices for multi-select list drags
	int32 SourceQueueIndex = INDEX_NONE;  // Set for queue-reorder drags

	/** The asset the indices belong to. Slate drag-drop is app-wide and the indices are meaningless
	 *  against any other asset; the Animation Map drop target rejects ops whose SourceAsset is not its
	 *  own (null fails CLOSED there). Stamped by the flipbook-list factories; queue-reorder drags
	 *  (NewFromQueue) deliberately leave it null — queue accept sites never check it. */
	TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> SourceAsset;

	static TSharedRef<FQueueDragDropOp> NewFromFlipbookList(int32 InFlipbookIndex, const FString& AnimName,
		UPaper2DPlusCharacterProfileAsset* InSourceAsset)
	{
		TSharedRef<FQueueDragDropOp> Op = MakeShareable(new FQueueDragDropOp());
		Op->FlipbookIndex = InFlipbookIndex;
		Op->FlipbookIndices.Add(InFlipbookIndex);
		Op->SourceAsset = InSourceAsset;
		Op->DefaultHoverText = FText::FromString(AnimName);
		Op->Construct();
		return Op;
	}

	static TSharedRef<FQueueDragDropOp> NewFromFlipbookListMulti(const TArray<int32>& InIndices, const FString& Label,
		UPaper2DPlusCharacterProfileAsset* InSourceAsset)
	{
		TSharedRef<FQueueDragDropOp> Op = MakeShareable(new FQueueDragDropOp());
		Op->FlipbookIndices = InIndices;
		Op->FlipbookIndex = InIndices.Num() > 0 ? InIndices[0] : INDEX_NONE;
		Op->SourceAsset = InSourceAsset;
		Op->DefaultHoverText = FText::FromString(Label);
		Op->Construct();
		return Op;
	}

	static TSharedRef<FQueueDragDropOp> NewFromQueue(int32 InQueueIndex, int32 InFlipbookIndex, const FString& AnimName)
	{
		TSharedRef<FQueueDragDropOp> Op = MakeShareable(new FQueueDragDropOp());
		Op->SourceQueueIndex = InQueueIndex;
		Op->FlipbookIndex = InFlipbookIndex;
		Op->DefaultHoverText = FText::FromString(AnimName);
		Op->Construct();
		return Op;
	}

	bool IsFromFlipbookList() const { return FlipbookIndices.Num() > 0 && SourceQueueIndex == INDEX_NONE; }
	bool IsFromQueue() const { return SourceQueueIndex != INDEX_NONE; }

	virtual TSharedPtr<SWidget> GetDefaultDecorator() const override
	{
		return SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.DarkGroupBorder"))
			.Padding(FMargin(6, 2))
			[
				SNew(STextBlock).Text(DefaultHoverText)
			];
	}

private:
	FText DefaultHoverText;
};

// ---------------------------------------------------------------------------
// Widget wrapper that enables drag-to-reorder on queue entries
// Also accepts flipbook-list-to-queue drops for insertion at position
// ---------------------------------------------------------------------------
class SQueueEntryDragDropWrapper : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SQueueEntryDragDropWrapper) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	int32 QueueIndex = INDEX_NONE;
	int32 FlipbookIndex = INDEX_NONE;
	FString FlipbookName;
	TFunction<void()> OnClickedFunc;
	TFunction<void()> OnRightClickFunc;
	TFunction<void(int32, int32)> OnQueueReorderFunc;    // (FromQueueIdx, ToQueueIdx)
	TFunction<void(const TArray<int32>&, int32)> OnAnimsDroppedFunc; // (FlipbookIndices, InsertAtQueueIdx)

	void Construct(const FArguments& InArgs)
	{
		ChildSlot
		[
			SNew(SVerticalBox)

			// Drop indicator line
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SAssignNew(DropIndicator, SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
				.BorderBackgroundColor(FLinearColor(0.3f, 0.6f, 1.0f))
				.Padding(0)
				.Visibility(EVisibility::Collapsed)
				[
					SNew(SBox)
					.HeightOverride(2)
				]
			]

			// Actual content
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				InArgs._Content.Widget
			]
		];
	}

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
		{
			bPotentialDrag = true;
			DragStartPos = MouseEvent.GetScreenSpacePosition();
			return FReply::Handled().CaptureMouse(SharedThis(this));
		}
		return FReply::Unhandled();
	}

	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (bPotentialDrag && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
		{
			bPotentialDrag = false;
			if (OnClickedFunc) { OnClickedFunc(); }
			return FReply::Handled().ReleaseMouseCapture();
		}
		if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
		{
			if (OnRightClickFunc) { OnRightClickFunc(); }
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}

	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (bPotentialDrag)
		{
			float Distance = FVector2D::Distance(MouseEvent.GetScreenSpacePosition(), DragStartPos);
			if (Distance > 5.0f)
			{
				bPotentialDrag = false;
				return FReply::Handled()
					.ReleaseMouseCapture()
					.BeginDragDrop(FQueueDragDropOp::NewFromQueue(QueueIndex, FlipbookIndex, FlipbookName));
			}
		}
		return FReply::Unhandled();
	}

	virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override
	{
		bPotentialDrag = false;
	}

	virtual void OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override
	{
		if (DragDropEvent.GetOperationAs<FQueueDragDropOp>().IsValid())
		{
			DropIndicator->SetVisibility(EVisibility::Visible);
		}
	}

	virtual void OnDragLeave(const FDragDropEvent& DragDropEvent) override
	{
		DropIndicator->SetVisibility(EVisibility::Collapsed);
	}

	virtual FReply OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override
	{
		if (DragDropEvent.GetOperationAs<FQueueDragDropOp>().IsValid())
		{
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}

	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override
	{
		DropIndicator->SetVisibility(EVisibility::Collapsed);
		TSharedPtr<FQueueDragDropOp> Op = DragDropEvent.GetOperationAs<FQueueDragDropOp>();
		if (Op.IsValid())
		{
			if (Op->IsFromQueue() && Op->SourceQueueIndex != QueueIndex && OnQueueReorderFunc)
			{
				OnQueueReorderFunc(Op->SourceQueueIndex, QueueIndex);
				return FReply::Handled();
			}
			else if (Op->IsFromFlipbookList() && OnAnimsDroppedFunc)
			{
				OnAnimsDroppedFunc(Op->FlipbookIndices, QueueIndex);
				return FReply::Handled();
			}
		}
		return FReply::Unhandled();
	}

private:
	bool bPotentialDrag = false;
	FVector2D DragStartPos;
	TSharedPtr<SBorder> DropIndicator;
};
