// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CharacterDataAssetEditor.h"
#include "EditorCanvasUtils.h"
#include "Input/DragAndDrop.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Images/SImage.h"
#include "PaperFlipbook.h"
#include "PaperSprite.h"
#include "SpriteEditorOnlyTypes.h"
#include "Engine/Texture2D.h"
#include "Paper2DPlusSettings.h"
#include "Framework/Application/SlateApplication.h"
#include "PropertyCustomizationHelpers.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Input/SSearchBox.h"

#define LOCTEXT_NAMESPACE "CharacterDataAssetEditor"

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
			.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
			.Padding(FMargin(6, 2))
			[
				SNew(STextBlock)
				.Text(FText::Format(LOCTEXT("DragFrameLabel", "Frame {0}"), FText::AsNumber(SourceFrameIndex)))
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
	TFunction<void()> OnClickedFunc;
	TFunction<void(int32, int32)> OnFrameDroppedFunc;

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
					.BeginDragDrop(FFrameReorderDragDropOp::New(FrameIndex));
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
	bool bPotentialDrag = false;
	FVector2D DragStartPos;
	TSharedPtr<SBorder> DropIndicator;
};

// ---------------------------------------------------------------------------
// Unified drag-drop operation for queue interactions
// Handles both animation-list-to-queue drags and queue-reorder drags
// ---------------------------------------------------------------------------
class FQueueDragDropOp : public FDragDropOperation
{
public:
	DRAG_DROP_OPERATOR_TYPE(FQueueDragDropOp, FDragDropOperation)

	int32 AnimationIndex = INDEX_NONE;    // Set for animation-list-to-queue drags
	int32 SourceQueueIndex = INDEX_NONE;  // Set for queue-reorder drags

	static TSharedRef<FQueueDragDropOp> NewFromAnimationList(int32 InAnimIndex, const FString& AnimName)
	{
		TSharedRef<FQueueDragDropOp> Op = MakeShareable(new FQueueDragDropOp());
		Op->AnimationIndex = InAnimIndex;
		Op->DefaultHoverText = FText::FromString(AnimName);
		Op->Construct();
		return Op;
	}

	static TSharedRef<FQueueDragDropOp> NewFromQueue(int32 InQueueIndex, int32 InAnimIndex, const FString& AnimName)
	{
		TSharedRef<FQueueDragDropOp> Op = MakeShareable(new FQueueDragDropOp());
		Op->SourceQueueIndex = InQueueIndex;
		Op->AnimationIndex = InAnimIndex;
		Op->DefaultHoverText = FText::FromString(AnimName);
		Op->Construct();
		return Op;
	}

	bool IsFromAnimationList() const { return AnimationIndex != INDEX_NONE && SourceQueueIndex == INDEX_NONE; }
	bool IsFromQueue() const { return SourceQueueIndex != INDEX_NONE; }

	virtual TSharedPtr<SWidget> GetDefaultDecorator() const override
	{
		return SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
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
// Also accepts animation-list-to-queue drops for insertion at position
// ---------------------------------------------------------------------------
class SQueueEntryDragDropWrapper : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SQueueEntryDragDropWrapper) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	int32 QueueIndex = INDEX_NONE;
	int32 AnimationIndex = INDEX_NONE;
	FString AnimationName;
	TFunction<void()> OnClickedFunc;
	TFunction<void()> OnRightClickFunc;
	TFunction<void(int32, int32)> OnQueueReorderFunc;    // (FromQueueIdx, ToQueueIdx)
	TFunction<void(int32, int32)> OnAnimDroppedFunc;     // (AnimIndex, InsertAtQueueIdx)

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
					.BeginDragDrop(FQueueDragDropOp::NewFromQueue(QueueIndex, AnimationIndex, AnimationName));
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
			else if (Op->IsFromAnimationList() && OnAnimDroppedFunc)
			{
				OnAnimDroppedFunc(Op->AnimationIndex, QueueIndex);
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

// ---------------------------------------------------------------------------
// Widget wrapper that enables drag-from-animation-list into queue
// ---------------------------------------------------------------------------
class SAnimListDragWrapper : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAnimListDragWrapper) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	int32 AnimationIndex = INDEX_NONE;
	FString AnimationName;
	TFunction<void()> OnClickedFunc;
	TFunction<void(const FGeometry&, const FPointerEvent&)> OnRightClickFunc;

	void Construct(const FArguments& InArgs)
	{
		ChildSlot
		[
			InArgs._Content.Widget
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
		if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
		{
			if (OnRightClickFunc) { OnRightClickFunc(MyGeometry, MouseEvent); }
			return FReply::Handled();
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
					.BeginDragDrop(FQueueDragDropOp::NewFromAnimationList(AnimationIndex, AnimationName));
			}
		}
		return FReply::Unhandled();
	}

	virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override
	{
		bPotentialDrag = false;
	}

private:
	bool bPotentialDrag = false;
	FVector2D DragStartPos;
};

TSharedRef<SWidget> SCharacterDataAssetEditor::BuildAlignmentEditorTab()
{
	return SNew(SVerticalBox)

		// Unified toolbar
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			BuildAlignmentToolbar()
		]

		// Main content area
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(4)
		[
			SNew(SSplitter)
			.Orientation(Orient_Horizontal)

			// Left panel: Animation and Frame lists
			+ SSplitter::Slot()
			.Value(0.2f)
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.FillHeight(0.35f)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					.Padding(4)
					[
						BuildAlignmentAnimationList()
					]
				]

				+ SVerticalBox::Slot()
				.FillHeight(0.25f)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					.Padding(4)
					[
						BuildPlaybackQueuePanel()
					]
				]

				+ SVerticalBox::Slot()
				.FillHeight(0.40f)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					.Padding(4)
					[
						BuildAlignmentFrameList()
					]
				]
			]

			// Center: Canvas
			+ SSplitter::Slot()
			.Value(0.6f)
			[
				BuildAlignmentCanvasArea()
			]

			// Right panel: Offset controls
			+ SSplitter::Slot()
			.Value(0.2f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				.Padding(4)
				[
					SNew(SScrollBox)
					+ SScrollBox::Slot()
					[
						BuildOffsetControlsPanel()
					]
				]
			]
		];
}

TSharedRef<SWidget> SCharacterDataAssetEditor::BuildAlignmentToolbar()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(FMargin(8, 4))
		[
			SNew(SHorizontalBox)

			// === SECTION 1: Flipbook Name ===
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 8, 0)
			[
				SNew(STextBlock)
				.Text_Lambda([this]() {
					const FAnimationHitboxData* Anim = GetCurrentAnimation();
					return Anim ? FText::FromString(Anim->AnimationName) : LOCTEXT("NoFlipbook", "None");
				})
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
			]

			// Separator
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(8, 0)
			[
				SNew(SSeparator)
				.Orientation(Orient_Vertical)
			]

			// === SECTION 2: Frame Navigation ===
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 0, 2, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.ToolTipText(LOCTEXT("PrevFrameTooltip", "Previous Frame (Left Arrow or ,)"))
				.OnClicked_Lambda([this]() {
					OnPrevFrameClicked();
					RefreshAlignmentFrameList();
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text(LOCTEXT("PrevFrame", "<"))
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4, 0)
			[
				SNew(STextBlock)
				.Text_Lambda([this]() {
					int32 FrameCount = GetCurrentFrameCount();
					return FText::Format(LOCTEXT("FrameCounter", "{0}/{1}"),
						FText::AsNumber(SelectedFrameIndex + 1),
						FText::AsNumber(FrameCount));
				})
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 0, 8, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.ToolTipText(LOCTEXT("NextFrameTooltip", "Next Frame (Right Arrow or .)"))
				.OnClicked_Lambda([this]() {
					OnNextFrameClicked();
					RefreshAlignmentFrameList();
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text(LOCTEXT("NextFrame", ">"))
				]
			]

			// Separator
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(8, 0)
			[
				SNew(SSeparator)
				.Orientation(Orient_Vertical)
			]

			// === SECTION 3: Playback Controls ===
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 0, 8, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.ToolTipText(LOCTEXT("PlayPauseTooltip", "Play/Pause (Space)"))
				.OnClicked_Lambda([this]() {
					TogglePlayback();
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text_Lambda([this]() { return bIsPlaying ? LOCTEXT("Pause", "Pause") : LOCTEXT("Play", "Play"); })
				]
			]

			// === SECTION 4: Zoom ===
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 4, 0)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ZoomLabel", "Zoom:"))
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 0, 4, 0)
			[
				SNew(SBox)
				.WidthOverride(80)
				[
					SNew(SSlider)
					.ToolTipText(LOCTEXT("AlignmentZoomTooltip", "Zoom level for the sprite preview. Use mouse wheel to zoom, or drag this slider."))
					.MinValue(0.5f)
					.MaxValue(4.0f)
					.Value_Lambda([this]() { return AlignmentZoomLevel; })
					.OnValueChanged_Lambda([this](float NewValue) { AlignmentZoomLevel = NewValue; })
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 8, 0)
			[
				SNew(STextBlock)
				.Text_Lambda([this]() {
					return FText::Format(LOCTEXT("ZoomPercent", "{0}%"), FText::AsNumber(FMath::RoundToInt(AlignmentZoomLevel * 100)));
				})
			]

			// Separator
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(8, 0)
			[
				SNew(SSeparator)
				.Orientation(Orient_Vertical)
			]

			// === SECTION 5: View Options ===
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 0, 12, 0)
			[
				SNew(SCheckBox)
				.ToolTipText(LOCTEXT("GridTooltip", "Toggle Grid (G)"))
				.IsChecked_Lambda([this]() { return bShowAlignmentGrid ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { bShowAlignmentGrid = (State == ECheckBoxState::Checked); })
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ShowGridG", "Grid (G)"))
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 0, 8, 0)
			[
				SNew(SCheckBox)
				.ToolTipText(LOCTEXT("OnionTooltip", "Toggle Onion Skin (O)"))
				.IsChecked_Lambda([this]() { return bShowOnionSkin ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { bShowOnionSkin = (State == ECheckBoxState::Checked); })
				[
					SNew(STextBlock)
					.Text(LOCTEXT("OnionSkinO", "Onion (O)"))
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 4, 0)
			[
				SNew(SBox)
				.WidthOverride(40)
				[
					SNew(SSpinBox<int32>)
					.ToolTipText(LOCTEXT("OnionFramesTooltip", "Number of onion skin frames"))
					.MinValue(1)
					.MaxValue(3)
					.Value_Lambda([this]() { return OnionSkinFrames; })
					.OnValueChanged_Lambda([this](int32 NewValue) { OnionSkinFrames = NewValue; })
					.IsEnabled_Lambda([this]() { return bShowOnionSkin; })
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 0, 8, 0)
			[
				SNew(SBox)
				.WidthOverride(60)
				[
					SNew(SSlider)
					.ToolTipText(LOCTEXT("OnionOpacityTooltip", "Onion skin opacity"))
					.MinValue(0.1f)
					.MaxValue(0.8f)
					.Value_Lambda([this]() { return OnionSkinOpacity; })
					.OnValueChanged_Lambda([this](float NewValue) { OnionSkinOpacity = NewValue; })
					.IsEnabled_Lambda([this]() { return bShowOnionSkin; })
				]
			]

			// Separator
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(8, 0)
			[
				SNew(SSeparator)
				.Orientation(Orient_Vertical)
			]

			// === SECTION 5.5: Reference Sprite ===
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 0, 8, 0)
			[
				SNew(SCheckBox)
				.ToolTipText(LOCTEXT("RefSpriteTooltip", "Toggle Reference Sprite overlay (R). Shows a persistent reference frame from any animation for alignment comparison."))
				.IsChecked_Lambda([this]() { return bShowReferenceSprite && ReferenceAnimationIndex != INDEX_NONE ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State) {
					if (State == ECheckBoxState::Checked)
					{
						if (ReferenceAnimationIndex == INDEX_NONE)
						{
							// No reference set yet — capture current frame
							SetReferenceSprite(SelectedAnimationIndex, SelectedFrameIndex);
						}
						else
						{
							bShowReferenceSprite = true;
						}
					}
					else
					{
						bShowReferenceSprite = false;
					}
				})
				[
					SNew(STextBlock)
					.Text_Lambda([this]() {
						if (bShowReferenceSprite && ReferenceAnimationIndex != INDEX_NONE && Asset.IsValid()
							&& Asset->Animations.IsValidIndex(ReferenceAnimationIndex))
						{
							return FText::Format(LOCTEXT("RefLabelActive", "Ref: {0} #{1}"),
								FText::FromString(Asset->Animations[ReferenceAnimationIndex].AnimationName),
								FText::AsNumber(ReferenceFrameIndex));
						}
						return LOCTEXT("RefLabelInactive", "Ref (R)");
					})
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 0, 4, 0)
			[
				SNew(SBox)
				.WidthOverride(60)
				[
					SNew(SSlider)
					.ToolTipText(LOCTEXT("RefOpacityTooltip", "Reference sprite opacity"))
					.MinValue(0.1f)
					.MaxValue(0.8f)
					.Value_Lambda([this]() { return ReferenceSpriteOpacity; })
					.OnValueChanged_Lambda([this](float NewValue) { ReferenceSpriteOpacity = NewValue; })
					.IsEnabled_Lambda([this]() { return bShowReferenceSprite && ReferenceAnimationIndex != INDEX_NONE; })
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 0, 8, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.ToolTipText(LOCTEXT("ClearRefTooltip", "Clear reference sprite"))
				.IsEnabled_Lambda([this]() { return ReferenceAnimationIndex != INDEX_NONE; })
				.OnClicked_Lambda([this]() {
					ClearReferenceSprite();
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ClearRef", "Clear"))
				]
			]

			// Separator
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(8, 0)
			[
				SNew(SSeparator)
				.Orientation(Orient_Vertical)
			]

			// === SECTION 6: Anchor ===
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 4, 0)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("AnchorLabel", "Anchor:"))
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 0, 0, 0)
			[
				SNew(SBox)
				.WidthOverride(100)
				.ToolTipText(LOCTEXT("AnchorSelectorTooltip", "The alignment anchor point (reticle position). This is the reference point sprites are aligned around. Common choices: BottomCenter for platformers, Center for top-down games."))
				[
					SNew(SComboButton)
					.OnGetMenuContent_Lambda([this]() -> TSharedRef<SWidget> {
						FMenuBuilder MenuBuilder(true, nullptr);

						auto AddAnchorOption = [this, &MenuBuilder](ESpriteAnchor Anchor, const FText& Label) {
							MenuBuilder.AddMenuEntry(
								Label,
								FText::GetEmpty(),
								FSlateIcon(),
								FUIAction(
									FExecuteAction::CreateLambda([this, Anchor]() { AlignmentReticleAnchor = Anchor; }),
									FCanExecuteAction(),
									FIsActionChecked::CreateLambda([this, Anchor]() { return AlignmentReticleAnchor == Anchor; })
								),
								NAME_None,
								EUserInterfaceActionType::RadioButton
							);
						};

						AddAnchorOption(ESpriteAnchor::TopLeft, LOCTEXT("TopLeft", "Top Left"));
						AddAnchorOption(ESpriteAnchor::TopCenter, LOCTEXT("TopCenter", "Top Center"));
						AddAnchorOption(ESpriteAnchor::TopRight, LOCTEXT("TopRight", "Top Right"));
						MenuBuilder.AddSeparator();
						AddAnchorOption(ESpriteAnchor::CenterLeft, LOCTEXT("CenterLeft", "Center Left"));
						AddAnchorOption(ESpriteAnchor::Center, LOCTEXT("Center", "Center"));
						AddAnchorOption(ESpriteAnchor::CenterRight, LOCTEXT("CenterRight", "Center Right"));
						MenuBuilder.AddSeparator();
						AddAnchorOption(ESpriteAnchor::BottomLeft, LOCTEXT("BottomLeft", "Bottom Left"));
						AddAnchorOption(ESpriteAnchor::BottomCenter, LOCTEXT("BottomCenter", "Bottom Center"));
						AddAnchorOption(ESpriteAnchor::BottomRight, LOCTEXT("BottomRight", "Bottom Right"));

						return MenuBuilder.MakeWidget();
					})
					.ButtonContent()
					[
						SNew(STextBlock)
						.Text_Lambda([this]() {
							switch (AlignmentReticleAnchor)
							{
								case ESpriteAnchor::TopLeft:      return LOCTEXT("TL", "Top Left");
								case ESpriteAnchor::TopCenter:    return LOCTEXT("TC", "Top Center");
								case ESpriteAnchor::TopRight:     return LOCTEXT("TR", "Top Right");
								case ESpriteAnchor::CenterLeft:   return LOCTEXT("CL", "Center Left");
								case ESpriteAnchor::Center:       return LOCTEXT("C", "Center");
								case ESpriteAnchor::CenterRight:  return LOCTEXT("CR", "Center Right");
								case ESpriteAnchor::BottomLeft:   return LOCTEXT("BL", "Bottom Left");
								case ESpriteAnchor::BottomCenter: return LOCTEXT("BC", "Bottom Center");
								case ESpriteAnchor::BottomRight:  return LOCTEXT("BR", "Bottom Right");
								default: return LOCTEXT("Unknown", "Unknown");
							}
						})
					]
				]
			]

			// Separator
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(8, 0)
			[
				SNew(SSeparator)
				.Orientation(Orient_Vertical)
			]

			// Spacer
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNullWidget::NullWidget
			]
		];
}

TSharedRef<SWidget> SCharacterDataAssetEditor::BuildAlignmentAnimationList()
{
	return SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 0, 0, 4)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("AlignFlipbooks", "FLIPBOOKS"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
		]

		// Search bar
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 0, 0, 4)
		[
			SAssignNew(AlignmentAnimSearchBox, SSearchBox)
			.HintText(LOCTEXT("SearchAnimations", "Search..."))
			.OnTextChanged_Lambda([this](const FText& NewText) {
				AlignmentAnimSearchFilter = NewText.ToString();

				// Debounce: cancel previous timer, start new one
				if (AlignmentAnimSearchDebounceTimer.IsValid())
				{
					UnRegisterActiveTimer(AlignmentAnimSearchDebounceTimer.Pin().ToSharedRef());
				}
				AlignmentAnimSearchDebounceTimer = RegisterActiveTimer(0.2f,
					FWidgetActiveTimerDelegate::CreateLambda(
						[this](double, float) {
							RefreshAlignmentAnimationList();
							return EActiveTimerReturnType::Stop;
						}));
			})
		]

		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SAssignNew(AlignmentAnimationListBox, SVerticalBox)
			]
		];
}

TSharedRef<SWidget> SCharacterDataAssetEditor::BuildAlignmentFrameList()
{
	return SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 0, 0, 4)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("AlignFrames", "FRAMES"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
		]

		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SAssignNew(AlignmentFrameListBox, SVerticalBox)
			]
		];
}

TSharedRef<SWidget> SCharacterDataAssetEditor::BuildPlaybackQueuePanel()
{
	return SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 0, 0, 4)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("PlaybackQueue", "PLAYBACK QUEUE"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.ToolTipText(LOCTEXT("ClearQueue", "Clear queue"))
				.OnClicked_Lambda([this]() { ClearPlaybackQueue(); return FReply::Handled(); })
				.IsEnabled_Lambda([this]() { return PlaybackQueue.Num() > 0; })
				[
					SNew(STextBlock).Text(LOCTEXT("Clear", "Clear"))
				]
			]
		]

		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SOverlay)

			+ SOverlay::Slot()
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SAssignNew(PlaybackQueueListBox, SVerticalBox)
				]
			]

			// Empty state hint (visible only when queue is empty)
			+ SOverlay::Slot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("DragHint", "Drag animations here"))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Visibility_Lambda([this]() {
					return PlaybackQueue.Num() == 0 ? EVisibility::Visible : EVisibility::Collapsed;
				})
			]
		];
}

TSharedRef<SWidget> SCharacterDataAssetEditor::BuildAlignmentCanvasArea()
{
	TSharedRef<SBorder> Border = SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(0)
		[
			SAssignNew(AlignmentCanvas, SSpriteAlignmentCanvas)
			.Asset(Asset.Get())
			.SelectedAnimationIndex_Lambda([this]() { return SelectedAnimationIndex; })
			.SelectedFrameIndex_Lambda([this]() { return SelectedFrameIndex; })
			.ShowGrid_Lambda([this]() { return bShowAlignmentGrid; })
			.Zoom_Lambda([this]() { return AlignmentZoomLevel; })
			.ShowOnionSkin_Lambda([this]() { return bShowOnionSkin; })
			.OnionSkinFrames_Lambda([this]() { return OnionSkinFrames; })
			.OnionSkinOpacity_Lambda([this]() { return OnionSkinOpacity; })
			.PreviousAnimationIndex_Lambda([this]() { return GetAdjacentAnimationIndex(-1); })
			.ReticleAnchor_Lambda([this]() { return AlignmentReticleAnchor; })
			.FlipX_Lambda([this]() { return bSpriteFlipX; })
			.FlipY_Lambda([this]() { return bSpriteFlipY; })
			.ShowReferenceSprite_Lambda([this]() { return bShowReferenceSprite && ReferenceAnimationIndex != INDEX_NONE; })
			.ReferenceSprite_Lambda([this]() -> TWeakObjectPtr<UPaperSprite> {
				if (!bShowReferenceSprite || !Asset.IsValid()) return nullptr;
				if (!Asset->Animations.IsValidIndex(ReferenceAnimationIndex)) return nullptr;
				const FAnimationHitboxData& Anim = Asset->Animations[ReferenceAnimationIndex];
				if (Anim.Flipbook.IsNull()) return nullptr;
				UPaperFlipbook* FB = Anim.Flipbook.LoadSynchronous();
				if (!FB || ReferenceFrameIndex >= FB->GetNumKeyFrames()) return nullptr;
				return FB->GetKeyFrameChecked(ReferenceFrameIndex).Sprite;
			})
			.ReferenceSpriteOffset_Lambda([this]() -> FIntPoint {
				if (!Asset.IsValid() || !Asset->Animations.IsValidIndex(ReferenceAnimationIndex)) return FIntPoint::ZeroValue;
				const FAnimationHitboxData& Anim = Asset->Animations[ReferenceAnimationIndex];
				if (!Anim.FrameExtractionInfo.IsValidIndex(ReferenceFrameIndex)) return FIntPoint::ZeroValue;
				return Anim.FrameExtractionInfo[ReferenceFrameIndex].SpriteOffset;
			})
			.ReferenceSpriteOpacity_Lambda([this]() { return ReferenceSpriteOpacity; })
		];

	// Wire up canvas delegates
	if (AlignmentCanvas.IsValid())
	{
		AlignmentCanvas->OnOffsetChanged.BindSP(this, &SCharacterDataAssetEditor::OnAlignmentOffsetChanged);
		AlignmentCanvas->OnZoomChanged.BindLambda([this](float NewZoom) {
			AlignmentZoomLevel = NewZoom;
		});
		AlignmentCanvas->OnDragStarted.BindLambda([this]() {
			bAlignmentDragActive = true;
			// Transaction deferred until first actual offset change in NudgeOffset
		});
		AlignmentCanvas->OnDragEnded.BindLambda([this]() {
			if (bAlignmentDragActive)
			{
				if (ActiveTransaction.IsValid())
				{
					EndTransaction();
					RefreshAlignmentFrameList();
				}
				bAlignmentDragActive = false;
			}
		});
	}

	return Border;
}

TSharedRef<SWidget> SCharacterDataAssetEditor::BuildOffsetControlsPanel()
{
	return SNew(SVerticalBox)

		// === Section: Offset Values ===
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 0, 0, 4)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("OffsetControls", "OFFSET"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 0, 0, 12)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
			.Padding(8)
			[
				SNew(SVerticalBox)

				// X offset
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 0, 0, 4)
				[
					SNew(SHorizontalBox)
					.ToolTipText(LOCTEXT("OffsetXTooltip", "Horizontal offset in pixels. Positive values move the sprite right relative to the anchor point."))

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0, 0, 8, 0)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("OffsetX", "X:"))
					]

					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					[
						SNew(SSpinBox<int32>)
						.MinValue(-500)
						.MaxValue(500)
						.Value_Lambda([this]() {
							const FAnimationHitboxData* Anim = GetCurrentAnimation();
							if (Anim && Anim->FrameExtractionInfo.IsValidIndex(SelectedFrameIndex))
							{
								return Anim->FrameExtractionInfo[SelectedFrameIndex].SpriteOffset.X;
							}
							return 0;
						})
						.OnValueChanged_Lambda([this](int32 NewValue) { OnOffsetXChanged(NewValue); })
					]
				]

				// Y offset
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					.ToolTipText(LOCTEXT("OffsetYTooltip", "Vertical offset in pixels. Positive values move the sprite down relative to the anchor point."))

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0, 0, 8, 0)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("OffsetY", "Y:"))
					]

					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					[
						SNew(SSpinBox<int32>)
						.MinValue(-500)
						.MaxValue(500)
						.Value_Lambda([this]() {
							const FAnimationHitboxData* Anim = GetCurrentAnimation();
							if (Anim && Anim->FrameExtractionInfo.IsValidIndex(SelectedFrameIndex))
							{
								return Anim->FrameExtractionInfo[SelectedFrameIndex].SpriteOffset.Y;
							}
							return 0;
						})
						.OnValueChanged_Lambda([this](int32 NewValue) { OnOffsetYChanged(NewValue); })
					]
				]
			]
		]

		// === Section: Nudge Controls ===
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 0, 0, 4)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("NudgeControls", "NUDGE"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 0, 0, 4)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("NudgeHint", "WASD or Arrows (Shift = 10px)"))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 0, 0, 12)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.ToolTipText(LOCTEXT("NudgeUpTooltip", "Nudge Up (W or Up Arrow)"))
				.OnClicked_Lambda([this]() { NudgeOffset(0, -1); return FReply::Handled(); })
				[
					SNew(SBox)
					.WidthOverride(28)
					.HAlign(HAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("NudgeUp", "W"))
					]
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 2, 0)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
					.ToolTipText(LOCTEXT("NudgeLeftTooltip", "Nudge Left (A)"))
					.OnClicked_Lambda([this]() { NudgeOffset(-1, 0); return FReply::Handled(); })
					[
						SNew(SBox)
						.WidthOverride(28)
						.HAlign(HAlign_Center)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("NudgeLeft", "A"))
						]
					]
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 2, 0)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
					.ToolTipText(LOCTEXT("NudgeDownTooltip", "Nudge Down (S or Down Arrow)"))
					.OnClicked_Lambda([this]() { NudgeOffset(0, 1); return FReply::Handled(); })
					[
						SNew(SBox)
						.WidthOverride(28)
						.HAlign(HAlign_Center)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("NudgeDown", "S"))
						]
					]
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
					.ToolTipText(LOCTEXT("NudgeRightTooltip", "Nudge Right (D)"))
					.OnClicked_Lambda([this]() { NudgeOffset(1, 0); return FReply::Handled(); })
					[
						SNew(SBox)
						.WidthOverride(28)
						.HAlign(HAlign_Center)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("NudgeRight", "D"))
						]
					]
				]
			]
		]

		// === Section: Clipboard ===
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 0, 0, 4)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("Clipboard", "CLIPBOARD"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 0, 0, 12)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(0, 0, 2, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.HAlign(HAlign_Center)
				.ToolTipText(LOCTEXT("CopyOffsetTooltip", "Copy the current frame's offset to the clipboard for pasting to other frames."))
				.OnClicked_Lambda([this]() { OnCopyOffset(); return FReply::Handled(); })
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Copy", "Copy"))
				]
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(2, 0, 0, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.HAlign(HAlign_Center)
				.ToolTipText(LOCTEXT("PasteOffsetTooltip", "Apply the copied offset to the current frame."))
				.IsEnabled_Lambda([this]() { return bHasCopiedOffset; })
				.OnClicked_Lambda([this]() { OnPasteOffset(); return FReply::Handled(); })
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Paste", "Paste"))
				]
			]
		]

		// === Section: Batch Operations ===
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 0, 0, 4)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("BatchOps", "BATCH"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 0, 0, 4)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.HAlign(HAlign_Center)
			.ToolTipText(LOCTEXT("ApplyToRemainingTooltip", "Apply current offset to all remaining frames"))
			.OnClicked_Lambda([this]() { OnApplyOffsetToRemaining(); return FReply::Handled(); })
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ApplyToRemaining", "Apply to Remaining"))
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 0, 0, 12)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.HAlign(HAlign_Center)
			.ToolTipText(LOCTEXT("ApplyToAllTooltip", "Apply current offset to all frames"))
			.OnClicked_Lambda([this]() { OnApplyOffsetToAll(); return FReply::Handled(); })
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ApplyToAll", "Apply to All"))
			]
		]

		// === Section: Apply to Flipbook ===
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 0, 0, 4)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("ApplyToFlipbookTitle", "APPLY TO FLIPBOOK"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 0, 0, 4)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("ApplyToFlipbookHint", "Bake offsets into sprite SourceUV"))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 0, 0, 12)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.Text(LOCTEXT("ApplyOffsets", "Apply Offsets to Flipbook"))
			.ToolTipText(LOCTEXT("ApplyOffsetsTooltip", "Apply alignment offsets to the actual sprite assets by shifting their SourceUV. Offsets will be reset to zero after applying."))
			.IsEnabled_Lambda([this]()
			{
				if (!Asset.IsValid() || !Asset->Animations.IsValidIndex(SelectedAnimationIndex)) return false;
				const FAnimationHitboxData& Anim = Asset->Animations[SelectedAnimationIndex];
				for (const FSpriteExtractionInfo& Info : Anim.FrameExtractionInfo)
				{
					if (Info.SpriteOffset != FIntPoint::ZeroValue) return true;
				}
				return false;
			})
			.OnClicked_Lambda([this]() -> FReply
			{
				if (!Asset.IsValid() || !Asset->Animations.IsValidIndex(SelectedAnimationIndex))
				{
					return FReply::Handled();
				}

				FAnimationHitboxData& Anim = Asset->Animations[SelectedAnimationIndex];
				UPaperFlipbook* Flipbook = Anim.Flipbook.LoadSynchronous();
				if (!Flipbook)
				{
					FNotificationInfo Notification(LOCTEXT("NoFlipbookForOffsets", "No flipbook assigned to this animation. Set a flipbook reference first."));
					Notification.ExpireDuration = 4.0f;
					Notification.bUseSuccessFailIcons = true;
					TSharedPtr<SNotificationItem> NotifItem = FSlateNotificationManager::Get().AddNotification(Notification);
					if (NotifItem.IsValid()) { NotifItem->SetCompletionState(SNotificationItem::CS_Fail); }
					return FReply::Handled();
				}

				// Check if any offsets are non-zero
				bool bHasOffsets = false;
				for (const FSpriteExtractionInfo& Info : Anim.FrameExtractionInfo)
				{
					if (Info.SpriteOffset != FIntPoint::ZeroValue) { bHasOffsets = true; break; }
				}
				if (!bHasOffsets)
				{
					FNotificationInfo Notification(LOCTEXT("NoOffsetsToApply", "No offsets to apply"));
					Notification.ExpireDuration = 2.0f;
					FSlateNotificationManager::Get().AddNotification(Notification);
					return FReply::Handled();
				}

				FScopedTransaction Transaction(LOCTEXT("ApplyAlignmentOffsets", "Apply Alignment Offsets"));
				Asset->Modify();

				int32 NumFrames = FMath::Min(Anim.FrameExtractionInfo.Num(), Flipbook->GetNumKeyFrames());
				int32 AppliedCount = 0;
				TSet<UPaperSprite*> ProcessedSprites;

				for (int32 FrameIdx = 0; FrameIdx < NumFrames; ++FrameIdx)
				{
					FSpriteExtractionInfo& Info = Anim.FrameExtractionInfo[FrameIdx];
					if (Info.SpriteOffset == FIntPoint::ZeroValue) continue;

					UPaperSprite* Sprite = Flipbook->GetKeyFrameChecked(FrameIdx).Sprite;
					if (!Sprite) continue;

					// Skip sprites already processed (same sprite in multiple keyframes)
					if (ProcessedSprites.Contains(Sprite))
					{
						Info.SpriteOffset = FIntPoint::ZeroValue;
						Info.bHasCustomAlignment = false;
						continue;
					}
					ProcessedSprites.Add(Sprite);

					// Apply offset via custom pivot point — this shifts where the sprite
					// is anchored when rendered, without modifying the source rectangle.
					// Moving the pivot opposite to the offset makes content appear shifted
					// in the offset direction (pivot LEFT → content renders RIGHT).
					Sprite->Modify();
					FVector2D CurrentPivot = Sprite->GetPivotPosition();
					FVector2D NewPivot = CurrentPivot - FVector2D(Info.SpriteOffset.X, Info.SpriteOffset.Y);
					Sprite->SetPivotMode(ESpritePivotMode::Custom, NewPivot);
					Sprite->PostEditChange();
					Sprite->GetPackage()->MarkPackageDirty();

					Info.SpriteOffset = FIntPoint::ZeroValue;
					Info.bHasCustomAlignment = false;
					AppliedCount++;
				}

				Asset->MarkPackageDirty();
				Flipbook->PostEditChange();
				Flipbook->GetPackage()->MarkPackageDirty();

				RefreshAlignmentFrameList();

				FText NotifText = FText::Format(LOCTEXT("AppliedOffsets", "Applied offsets to {0} sprites"), AppliedCount);
				FNotificationInfo Notification(NotifText);
				Notification.ExpireDuration = 3.0f;
				Notification.bUseSuccessFailIcons = true;
				TSharedPtr<SNotificationItem> NotifItem = FSlateNotificationManager::Get().AddNotification(Notification);
				if (NotifItem.IsValid()) { NotifItem->SetCompletionState(SNotificationItem::CS_Success); }

				return FReply::Handled();
			})
		]

		// === Section: Sprite Flip ===
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 0, 0, 4)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("SpriteFlipTitle", "SPRITE FLIP"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 0, 0, 4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 0, 8, 0)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]() { return bSpriteFlipX ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { bSpriteFlipX = (State == ECheckBoxState::Checked); })
				[
					SNew(STextBlock).Text(LOCTEXT("SpriteFlipX", "Flip X"))
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]() { return bSpriteFlipY ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { bSpriteFlipY = (State == ECheckBoxState::Checked); })
				[
					SNew(STextBlock).Text(LOCTEXT("SpriteFlipY", "Flip Y"))
				]
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 0, 0, 4)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.HAlign(HAlign_Center)
			.OnClicked_Lambda([this]() { OnApplyFlipToCurrentFrame(); return FReply::Handled(); })
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ApplyFlipCurrentFrame", "Apply to Current Frame"))
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 0, 0, 4)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.HAlign(HAlign_Center)
			.OnClicked_Lambda([this]() { OnApplyFlipToCurrentAnimation(); return FReply::Handled(); })
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ApplyFlipCurrentAnimation", "Apply to Animation"))
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 0, 0, 12)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.HAlign(HAlign_Center)
			.OnClicked_Lambda([this]() { OnApplyFlipToAllAnimations(); return FReply::Handled(); })
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ApplyFlipAllAnimations", "Apply to All Animations"))
			]
		]

		// Spacer
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNullWidget::NullWidget
		]

		// === Reset at bottom ===
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.HAlign(HAlign_Center)
			.ToolTipText(LOCTEXT("ResetOffsetTooltip", "Reset offset to zero"))
			.OnClicked_Lambda([this]() { OnResetOffset(); return FReply::Handled(); })
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ResetOffset", "Reset Offset"))
			]
		];
}

void SCharacterDataAssetEditor::RefreshAlignmentAnimationList()
{
	if (!AlignmentAnimationListBox.IsValid() || !Asset.IsValid()) return;

	AlignmentAnimationListBox->ClearChildren();
	AlignmentAnimNameTexts.Empty();

	for (int32 i = 0; i < Asset->Animations.Num(); i++)
	{
		const FAnimationHitboxData& Anim = Asset->Animations[i];

		// Apply search filter
		if (!AlignmentAnimSearchFilter.IsEmpty()
			&& !Anim.AnimationName.Contains(AlignmentAnimSearchFilter, ESearchCase::IgnoreCase))
		{
			AlignmentAnimNameTexts.Add(nullptr); // Keep indices in sync
			continue;
		}

		bool bSelected = (i == SelectedAnimationIndex);

		TSharedPtr<SInlineEditableTextBlock> NameText;

		// Use SAnimListDragWrapper instead of SBorder > SButton so drag-to-queue works
		TSharedPtr<SAnimListDragWrapper> Wrapper;

		AlignmentAnimationListBox->AddSlot()
		.AutoHeight()
		.Padding(0, 0, 0, 2)
		[
			SAssignNew(Wrapper, SAnimListDragWrapper)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush(bSelected ? "ToolPanel.DarkGroupBorder" : "NoBorder"))
				.BorderBackgroundColor_Lambda([this, i]() -> FSlateColor {
					return (i == SelectedAnimationIndex) ? FLinearColor(0.2f, 0.4f, 0.8f, 0.5f) : FLinearColor::Transparent;
				})
				.Padding(FMargin(8, 4))
				[
					SAssignNew(NameText, SInlineEditableTextBlock)
					.Text(FText::FromString(Anim.AnimationName))
					.OnTextCommitted_Lambda([this, i](const FText& NewText, ETextCommit::Type CommitType)
					{
						if (CommitType != ETextCommit::OnCleared)
						{
							RenameAnimation(i, NewText.ToString());
						}
					})
				]
			]
		];

		Wrapper->AnimationIndex = i;
		Wrapper->AnimationName = Anim.AnimationName;
		Wrapper->OnClickedFunc = [this, i]() {
			OnAnimationSelected(i);
			RefreshAlignmentAnimationList();
			RefreshAlignmentFrameList();
		};
		Wrapper->OnRightClickFunc = [this, i](const FGeometry&, const FPointerEvent&) {
			OnAnimationSelected(i);
			RefreshAlignmentAnimationList();
			RefreshAlignmentFrameList();
			ShowAnimationContextMenu(i);
		};

		AlignmentAnimNameTexts.Add(NameText);
	}

	// Trigger pending rename
	if (PendingRenameAnimationIndex != INDEX_NONE)
	{
		int32 RenameIdx = PendingRenameAnimationIndex;
		PendingRenameAnimationIndex = INDEX_NONE;

		if (AlignmentAnimNameTexts.IsValidIndex(RenameIdx) && AlignmentAnimNameTexts[RenameIdx].IsValid())
		{
			TWeakPtr<SInlineEditableTextBlock> WeakText = AlignmentAnimNameTexts[RenameIdx];
			RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateLambda(
				[WeakText](double, float) -> EActiveTimerReturnType
				{
					if (TSharedPtr<SInlineEditableTextBlock> Text = WeakText.Pin())
					{
						Text->EnterEditingMode();
					}
					return EActiveTimerReturnType::Stop;
				}));
		}
	}
}

void SCharacterDataAssetEditor::RefreshPlaybackQueueList()
{
	if (!PlaybackQueueListBox.IsValid() || !Asset.IsValid()) return;

	PlaybackQueueListBox->ClearChildren();

	for (int32 QueueIdx = 0; QueueIdx < PlaybackQueue.Num(); QueueIdx++)
	{
		int32 AnimIdx = PlaybackQueue[QueueIdx];
		if (!Asset->Animations.IsValidIndex(AnimIdx)) continue;

		const FAnimationHitboxData& Anim = Asset->Animations[AnimIdx];
		bool bIsActive = bIsPlaying && QueueIdx == PlaybackQueueIndex;

		TSharedPtr<SQueueEntryDragDropWrapper> Wrapper;

		PlaybackQueueListBox->AddSlot()
		.AutoHeight()
		.Padding(0, 0, 0, 2)
		[
			SAssignNew(Wrapper, SQueueEntryDragDropWrapper)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush(bIsActive ? "ToolPanel.DarkGroupBorder" : "NoBorder"))
				.BorderBackgroundColor(bIsActive ? FLinearColor(0.2f, 0.6f, 0.2f, 0.5f) : FLinearColor::Transparent)
				.Padding(FMargin(4, 2))
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(FText::FromString(Anim.AnimationName))
						.Font(bIsActive ? FCoreStyle::GetDefaultFontStyle("Bold", 9) : FCoreStyle::GetDefaultFontStyle("Regular", 9))
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
						.ToolTipText(LOCTEXT("RemoveFromQueue", "Remove from queue"))
						.OnClicked_Lambda([this, QueueIdx]() {
							RemoveFromPlaybackQueue(QueueIdx);
							return FReply::Handled();
						})
						[
							SNew(STextBlock).Text(FText::FromString(TEXT("X")))
						]
					]
				]
			]
		];

		Wrapper->QueueIndex = QueueIdx;
		Wrapper->AnimationIndex = AnimIdx;
		Wrapper->AnimationName = Anim.AnimationName;
		Wrapper->OnClickedFunc = [this, AnimIdx]() {
			OnAnimationSelected(AnimIdx);
			RefreshAlignmentAnimationList();
			RefreshAlignmentFrameList();
		};
		Wrapper->OnRightClickFunc = [this, QueueIdx]() {
			FMenuBuilder MenuBuilder(true, nullptr);

			MenuBuilder.AddMenuEntry(
				LOCTEXT("QueueMoveUp", "Move Up"),
				LOCTEXT("QueueMoveUpTooltip", "Move this entry up in the queue"),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateLambda([this, QueueIdx]() { ReorderQueueEntry(QueueIdx, QueueIdx - 1); }),
					FCanExecuteAction::CreateLambda([QueueIdx]() { return QueueIdx > 0; })
				)
			);

			MenuBuilder.AddMenuEntry(
				LOCTEXT("QueueMoveDown", "Move Down"),
				LOCTEXT("QueueMoveDownTooltip", "Move this entry down in the queue"),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateLambda([this, QueueIdx]() { ReorderQueueEntry(QueueIdx, QueueIdx + 1); }),
					FCanExecuteAction::CreateLambda([this, QueueIdx]() { return QueueIdx < PlaybackQueue.Num() - 1; })
				)
			);

			MenuBuilder.AddMenuSeparator();

			MenuBuilder.AddMenuEntry(
				LOCTEXT("QueueRemove", "Remove"),
				LOCTEXT("QueueRemoveTooltip", "Remove this entry from the queue"),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([this, QueueIdx]() { RemoveFromPlaybackQueue(QueueIdx); }))
			);

			FSlateApplication::Get().PushMenu(
				SharedThis(this),
				FWidgetPath(),
				MenuBuilder.MakeWidget(),
				FSlateApplication::Get().GetCursorPos(),
				FPopupTransitionEffect::ContextMenu
			);
		};
		Wrapper->OnQueueReorderFunc = [this](int32 From, int32 To) {
			ReorderQueueEntry(From, To);
		};
		Wrapper->OnAnimDroppedFunc = [this](int32 AnimIndex, int32 InsertAt) {
			// Insert animation at this position in the queue
			if (Asset.IsValid() && Asset->Animations.IsValidIndex(AnimIndex))
			{
				PlaybackQueue.Insert(AnimIndex, InsertAt);
				// Adjust PlaybackQueueIndex if inserting before current during playback
				if (bIsPlaying && InsertAt <= PlaybackQueueIndex)
				{
					PlaybackQueueIndex++;
				}
				RefreshPlaybackQueueList();
			}
		};
	}

	// Always-present drop target at end of queue (handles empty queue + append)
	{
		TSharedPtr<SQueueEntryDragDropWrapper> DropTarget;

		PlaybackQueueListBox->AddSlot()
		.AutoHeight()
		[
			SAssignNew(DropTarget, SQueueEntryDragDropWrapper)
			[
				SNew(SBox)
				.HeightOverride(24)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("NoBorder"))
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(PlaybackQueue.Num() == 0
							? LOCTEXT("DropAnimHere", "Drag animations here")
							: FText::GetEmpty())
						.Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
					]
				]
			]
		];

		DropTarget->QueueIndex = PlaybackQueue.Num();
		DropTarget->OnAnimDroppedFunc = [this](int32 AnimIndex, int32 InsertAt) {
			if (Asset.IsValid() && Asset->Animations.IsValidIndex(AnimIndex))
			{
				PlaybackQueue.Insert(AnimIndex, InsertAt);
				if (bIsPlaying && InsertAt <= PlaybackQueueIndex)
				{
					PlaybackQueueIndex++;
				}
				RefreshPlaybackQueueList();
			}
		};
		DropTarget->OnQueueReorderFunc = [this](int32 From, int32 To) {
			ReorderQueueEntry(From, To);
		};
	}
}

void SCharacterDataAssetEditor::RefreshAlignmentFrameList()
{
	if (!AlignmentFrameListBox.IsValid() || !Asset.IsValid()) return;

	AlignmentFrameListBox->ClearChildren();

	const FAnimationHitboxData* Anim = GetCurrentAnimation();
	if (!Anim) return;

	// Get the flipbook to extract frame sprites
	UPaperFlipbook* Flipbook = nullptr;
	if (!Anim->Flipbook.IsNull())
	{
		Flipbook = Anim->Flipbook.LoadSynchronous();
	}

	// Use flipbook frame count, capped to actual data
	int32 FrameCount = FMath::Min(GetCurrentFrameCount(), Anim->Frames.Num());

	for (int32 i = 0; i < FrameCount; i++)
	{
		const FFrameHitboxData* FramePtr = &Anim->Frames[i];
		bool bSelected = (i == SelectedFrameIndex);

		// Check if this frame has custom alignment
		bool bHasCustomAlign = false;
		if (Anim->FrameExtractionInfo.IsValidIndex(i))
		{
			bHasCustomAlign = Anim->FrameExtractionInfo[i].bHasCustomAlignment;
		}

		// Get sprite for this frame's thumbnail
		UPaperSprite* FrameSprite = nullptr;
		if (Flipbook && i < Flipbook->GetNumKeyFrames())
		{
			FrameSprite = Flipbook->GetKeyFrameChecked(i).Sprite;
		}

		TSharedPtr<SFrameDragDropWrapper> Wrapper;

		AlignmentFrameListBox->AddSlot()
		.AutoHeight()
		.Padding(0, 0, 0, 2)
		[
			SAssignNew(Wrapper, SFrameDragDropWrapper)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush(bSelected ? "Brushes.Primary" : "Brushes.Header"))
				.Padding(FMargin(8, 4))
				[
					SNew(SHorizontalBox)

					// Sprite thumbnail
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0, 0, 4, 0)
					[
						SNew(SBox)
						.WidthOverride(32)
						.HeightOverride(32)
						[
							FrameSprite
								? StaticCastSharedRef<SWidget>(SNew(SSpriteThumbnail).Sprite(FrameSprite))
								: StaticCastSharedRef<SWidget>(SNew(SBorder)
									.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
									.HAlign(HAlign_Center)
									.VAlign(VAlign_Center)
									[
										SNew(STextBlock)
										.Text(FText::AsNumber(i))
										.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
									])
						]
					]

					// Frame name
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(FramePtr && !FramePtr->FrameName.IsEmpty() ? FText::FromString(FramePtr->FrameName) : FText::Format(LOCTEXT("AlignFrameFmt", "Frame {0}"), FText::AsNumber(i)))
					]

					// Custom alignment indicator
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(bHasCustomAlign ? LOCTEXT("AlignedMark", "*") : FText::GetEmpty())
						.ColorAndOpacity(FLinearColor::Yellow)
					]
				]
			]
		];

		Wrapper->FrameIndex = i;
		Wrapper->OnClickedFunc = [this, i]()
		{
			OnFrameSelected(i);
			RefreshAlignmentFrameList();
		};
		Wrapper->OnFrameDroppedFunc = [this](int32 From, int32 To)
		{
			ReorderFrame(From, To);
		};
	}

}

void SCharacterDataAssetEditor::NudgeOffset(int32 DeltaX, int32 DeltaY)
{
	// Pause queue playback during offset editing
	if (bIsPlaying && PlaybackQueue.Num() > 0) StopPlayback();

	FAnimationHitboxData* Anim = GetCurrentAnimationMutable();
	if (!Anim) return;

	// Auto-populate FrameExtractionInfo if it doesn't cover this frame
	int32 RequiredSize = SelectedFrameIndex + 1;
	if (Anim->FrameExtractionInfo.Num() < RequiredSize)
	{
		Anim->FrameExtractionInfo.SetNum(RequiredSize);
	}

	// During drag, defer transaction start until first actual movement
	bool bInDragGesture = bAlignmentDragActive;
	if (!bInDragGesture)
	{
		BeginTransaction(LOCTEXT("NudgeOffset", "Nudge Sprite Offset"));
	}
	else if (!ActiveTransaction.IsValid())
	{
		BeginTransaction(LOCTEXT("DragOffset", "Drag Sprite Offset"));
	}

	Anim->FrameExtractionInfo[SelectedFrameIndex].SpriteOffset.X += DeltaX;
	Anim->FrameExtractionInfo[SelectedFrameIndex].SpriteOffset.Y += DeltaY;
	Anim->FrameExtractionInfo[SelectedFrameIndex].bHasCustomAlignment = true;

	if (!bInDragGesture)
	{
		EndTransaction();
		RefreshAlignmentFrameList();
	}
}

void SCharacterDataAssetEditor::OnOffsetXChanged(int32 NewValue)
{
	if (bIsPlaying && PlaybackQueue.Num() > 0) StopPlayback();

	FAnimationHitboxData* Anim = GetCurrentAnimationMutable();
	if (!Anim) return;

	int32 RequiredSize = SelectedFrameIndex + 1;
	if (Anim->FrameExtractionInfo.Num() < RequiredSize)
	{
		Anim->FrameExtractionInfo.SetNum(RequiredSize);
	}

	BeginTransaction(LOCTEXT("ChangeOffsetX", "Change Sprite Offset X"));
	Asset->Modify();

	Anim->FrameExtractionInfo[SelectedFrameIndex].SpriteOffset.X = NewValue;
	Anim->FrameExtractionInfo[SelectedFrameIndex].bHasCustomAlignment = true;

	EndTransaction();
	RefreshAlignmentFrameList();
}

void SCharacterDataAssetEditor::OnOffsetYChanged(int32 NewValue)
{
	if (bIsPlaying && PlaybackQueue.Num() > 0) StopPlayback();

	FAnimationHitboxData* Anim = GetCurrentAnimationMutable();
	if (!Anim) return;

	int32 RequiredSize = SelectedFrameIndex + 1;
	if (Anim->FrameExtractionInfo.Num() < RequiredSize)
	{
		Anim->FrameExtractionInfo.SetNum(RequiredSize);
	}

	BeginTransaction(LOCTEXT("ChangeOffsetY", "Change Sprite Offset Y"));
	Asset->Modify();

	Anim->FrameExtractionInfo[SelectedFrameIndex].SpriteOffset.Y = NewValue;
	Anim->FrameExtractionInfo[SelectedFrameIndex].bHasCustomAlignment = true;

	EndTransaction();
	RefreshAlignmentFrameList();
}

void SCharacterDataAssetEditor::OnCopyOffset()
{
	const FAnimationHitboxData* Anim = GetCurrentAnimation();
	if (!Anim || !Anim->FrameExtractionInfo.IsValidIndex(SelectedFrameIndex))
	{
		CopiedOffset = FIntPoint::ZeroValue;
		bHasCopiedOffset = true;
		return;
	}

	CopiedOffset = Anim->FrameExtractionInfo[SelectedFrameIndex].SpriteOffset;
	bHasCopiedOffset = true;
}

void SCharacterDataAssetEditor::OnPasteOffset()
{
	if (!bHasCopiedOffset) return;

	FAnimationHitboxData* Anim = GetCurrentAnimationMutable();
	if (!Anim) return;

	int32 RequiredSize = SelectedFrameIndex + 1;
	if (Anim->FrameExtractionInfo.Num() < RequiredSize)
	{
		Anim->FrameExtractionInfo.SetNum(RequiredSize);
	}

	BeginTransaction(LOCTEXT("PasteOffset", "Paste Sprite Offset"));
	Asset->Modify();

	Anim->FrameExtractionInfo[SelectedFrameIndex].SpriteOffset = CopiedOffset;
	Anim->FrameExtractionInfo[SelectedFrameIndex].bHasCustomAlignment = true;

	EndTransaction();
	RefreshAlignmentFrameList();
}

void SCharacterDataAssetEditor::OnApplyOffsetToAll()
{
	FAnimationHitboxData* Anim = GetCurrentAnimationMutable();
	if (!Anim) return;

	int32 FrameCount = GetCurrentFrameCount();
	if (FrameCount <= 0) return;

	// Ensure FrameExtractionInfo covers all frames
	if (Anim->FrameExtractionInfo.Num() < FrameCount)
	{
		Anim->FrameExtractionInfo.SetNum(FrameCount);
	}
	if (!Anim->FrameExtractionInfo.IsValidIndex(SelectedFrameIndex)) return;

	FIntPoint CurrentOffset = Anim->FrameExtractionInfo[SelectedFrameIndex].SpriteOffset;

	BeginTransaction(LOCTEXT("ApplyOffsetAll", "Apply Offset to All Frames"));
	Asset->Modify();

	for (int32 i = 0; i < Anim->FrameExtractionInfo.Num(); i++)
	{
		Anim->FrameExtractionInfo[i].SpriteOffset = CurrentOffset;
		Anim->FrameExtractionInfo[i].bHasCustomAlignment = true;
	}

	EndTransaction();
	RefreshAlignmentFrameList();
}

void SCharacterDataAssetEditor::OnApplyOffsetToRemaining()
{
	FAnimationHitboxData* Anim = GetCurrentAnimationMutable();
	if (!Anim) return;

	int32 FrameCount = GetCurrentFrameCount();
	if (FrameCount <= 0) return;

	// Ensure FrameExtractionInfo covers all frames
	if (Anim->FrameExtractionInfo.Num() < FrameCount)
	{
		Anim->FrameExtractionInfo.SetNum(FrameCount);
	}
	if (!Anim->FrameExtractionInfo.IsValidIndex(SelectedFrameIndex)) return;

	FIntPoint CurrentOffset = Anim->FrameExtractionInfo[SelectedFrameIndex].SpriteOffset;

	BeginTransaction(LOCTEXT("ApplyOffsetRemaining", "Apply Offset to Remaining Frames"));
	Asset->Modify();

	for (int32 i = SelectedFrameIndex; i < Anim->FrameExtractionInfo.Num(); i++)
	{
		Anim->FrameExtractionInfo[i].SpriteOffset = CurrentOffset;
		Anim->FrameExtractionInfo[i].bHasCustomAlignment = true;
	}

	EndTransaction();
	RefreshAlignmentFrameList();
}

void SCharacterDataAssetEditor::OnResetOffset()
{
	FAnimationHitboxData* Anim = GetCurrentAnimationMutable();
	if (!Anim || !Anim->FrameExtractionInfo.IsValidIndex(SelectedFrameIndex)) return;

	BeginTransaction(LOCTEXT("ResetOffset", "Reset Sprite Offset"));
	Asset->Modify();

	Anim->FrameExtractionInfo[SelectedFrameIndex].SpriteOffset = FIntPoint::ZeroValue;
	Anim->FrameExtractionInfo[SelectedFrameIndex].bHasCustomAlignment = false;

	EndTransaction();
	RefreshAlignmentFrameList();
}

void SCharacterDataAssetEditor::OnAlignmentOffsetChanged(int32 DeltaX, int32 DeltaY)
{
	NudgeOffset(DeltaX, DeltaY);
}

void SCharacterDataAssetEditor::StartPlayback()
{
	if (bIsPlaying) return;

	bIsPlaying = true;

	// Only reset if queue finished or starting fresh
	if (PlaybackQueue.Num() > 0)
	{
		if (PlaybackQueueIndex >= PlaybackQueue.Num())
		{
			PlaybackQueueIndex = 0;
			PlaybackPosition = 0.0f;
		}
		// else: resume from current position

		// Cache timing for current queue entry
		int32 AnimIdx = PlaybackQueue[PlaybackQueueIndex];
		if (Asset.IsValid() && Asset->Animations.IsValidIndex(AnimIdx))
		{
			UPaperFlipbook* FB = Asset->Animations[AnimIdx].Flipbook.LoadSynchronous();
			if (FB)
			{
				CachedPlaybackTiming = FFlipbookTimingData::ReadFromFlipbook(FB);
			}
		}
	}
	else
	{
		PlaybackPosition = 0.0f;
	}

	PlaybackTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateSP(this, &SCharacterDataAssetEditor::OnPlaybackTick),
		1.0f / 60.0f
	);

	RefreshPlaybackQueueList();
}

void SCharacterDataAssetEditor::StopPlayback()
{
	if (!bIsPlaying) return;

	bIsPlaying = false;
	if (PlaybackTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(PlaybackTickerHandle);
		PlaybackTickerHandle.Reset();
	}

	RefreshPlaybackQueueList();
}

void SCharacterDataAssetEditor::TogglePlayback()
{
	if (bIsPlaying)
	{
		StopPlayback();
	}
	else
	{
		StartPlayback();
	}
}

int32 SCharacterDataAssetEditor::FrameIndexFromPlaybackPosition(
	const FFlipbookTimingData& Timing, float Position) const
{
	float AccumulatedTime = 0.0f;
	for (int32 i = 0; i < Timing.FrameDurations.Num(); i++)
	{
		float FrameDur = Timing.GetFrameDurationSeconds(i);
		if (Position < AccumulatedTime + FrameDur)
		{
			return i;
		}
		AccumulatedTime += FrameDur;
	}
	return FMath::Max(0, Timing.FrameDurations.Num() - 1);
}

bool SCharacterDataAssetEditor::OnPlaybackTick(float DeltaTime)
{
	if (!Asset.IsValid()) return true;

	if (PlaybackQueue.Num() > 0)
	{
		return OnQueuePlaybackTick(DeltaTime);
	}

	// Single animation playback (time-based)
	const FAnimationHitboxData* Anim = GetCurrentAnimation();
	if (!Anim || Anim->Flipbook.IsNull()) return true;

	UPaperFlipbook* FB = Anim->Flipbook.LoadSynchronous();
	if (!FB) return true;

	FFlipbookTimingData Timing = FFlipbookTimingData::ReadFromFlipbook(FB);
	if (Timing.TotalDurationSeconds <= 0.0f) return true;

	PlaybackPosition += DeltaTime;
	if (PlaybackPosition >= Timing.TotalDurationSeconds)
	{
		PlaybackPosition = FMath::Fmod(PlaybackPosition, Timing.TotalDurationSeconds);
	}

	int32 NewFrameIndex = FrameIndexFromPlaybackPosition(Timing, PlaybackPosition);

	if (NewFrameIndex != SelectedFrameIndex)
	{
		SelectedFrameIndex = NewFrameIndex;
		RefreshAlignmentFrameList();
	}

	return true;
}

bool SCharacterDataAssetEditor::OnQueuePlaybackTick(float DeltaTime)
{
	// Validate current queue entry — skip invalid entries
	while (PlaybackQueueIndex < PlaybackQueue.Num())
	{
		int32 AnimIdx = PlaybackQueue[PlaybackQueueIndex];
		if (Asset->Animations.IsValidIndex(AnimIdx)
			&& !Asset->Animations[AnimIdx].Flipbook.IsNull())
		{
			break; // Valid entry found
		}
		// Skip invalid entry
		PlaybackQueueIndex++;
		PlaybackPosition = 0.0f;
		CachedPlaybackTiming = FFlipbookTimingData();
	}

	// If we ran past the end, loop the queue
	if (PlaybackQueueIndex >= PlaybackQueue.Num())
	{
		PlaybackQueueIndex = 0;
		PlaybackPosition = 0.0f;

		// Find first valid entry
		bool bFoundValid = false;
		for (int32 i = 0; i < PlaybackQueue.Num(); i++)
		{
			int32 AnimIdx = PlaybackQueue[i];
			if (Asset->Animations.IsValidIndex(AnimIdx)
				&& !Asset->Animations[AnimIdx].Flipbook.IsNull())
			{
				PlaybackQueueIndex = i;
				bFoundValid = true;
				break;
			}
		}
		if (!bFoundValid) return true;
		CachedPlaybackTiming = FFlipbookTimingData();
	}

	int32 AnimIdx = PlaybackQueue[PlaybackQueueIndex];
	FAnimationHitboxData& Anim = Asset->Animations[AnimIdx];

	// Use cached timing — only rebuild if invalid
	if (CachedPlaybackTiming.TotalDurationSeconds <= 0.0f)
	{
		UPaperFlipbook* FB = Anim.Flipbook.LoadSynchronous();
		if (!FB) { PlaybackQueueIndex++; PlaybackPosition = 0.0f; return true; }
		CachedPlaybackTiming = FFlipbookTimingData::ReadFromFlipbook(FB);
		if (CachedPlaybackTiming.TotalDurationSeconds <= 0.0f)
		{
			PlaybackQueueIndex++;
			PlaybackPosition = 0.0f;
			CachedPlaybackTiming = FFlipbookTimingData();
			return true;
		}
	}

	PlaybackPosition += DeltaTime;

	// Check if we've exceeded this animation's duration
	if (PlaybackPosition >= CachedPlaybackTiming.TotalDurationSeconds)
	{
		float Overflow = PlaybackPosition - CachedPlaybackTiming.TotalDurationSeconds;
		PlaybackQueueIndex++;
		PlaybackPosition = Overflow;
		CachedPlaybackTiming = FFlipbookTimingData(); // Invalidate for next entry

		// Sync selection to new animation
		if (PlaybackQueueIndex < PlaybackQueue.Num())
		{
			SyncSelectionToQueueEntry(PlaybackQueueIndex);
		}
		RefreshPlaybackQueueList();
		return true;
	}

	// Sync selection if animation changed
	if (AnimIdx != SelectedAnimationIndex)
	{
		SyncSelectionToQueueEntry(PlaybackQueueIndex);
	}

	// Determine frame from cached timing
	int32 NewFrameIndex = FrameIndexFromPlaybackPosition(CachedPlaybackTiming, PlaybackPosition);

	if (NewFrameIndex != SelectedFrameIndex)
	{
		SelectedFrameIndex = NewFrameIndex;
		RefreshAlignmentFrameList();
	}

	return true;
}

void SCharacterDataAssetEditor::SyncSelectionToQueueEntry(int32 QueueIndex)
{
	if (!PlaybackQueue.IsValidIndex(QueueIndex)) return;

	int32 AnimIdx = PlaybackQueue[QueueIndex];
	if (AnimIdx != SelectedAnimationIndex)
	{
		SelectedAnimationIndex = AnimIdx;
		SelectedFrameIndex = 0;
		RefreshAlignmentAnimationList();
		RefreshAlignmentFrameList();
		RefreshPlaybackQueueList();
	}
}

void SCharacterDataAssetEditor::AddToPlaybackQueue(int32 AnimationIndex)
{
	if (Asset.IsValid() && Asset->Animations.IsValidIndex(AnimationIndex))
	{
		PlaybackQueue.Add(AnimationIndex);
		RefreshPlaybackQueueList();
	}
}

void SCharacterDataAssetEditor::RemoveFromPlaybackQueue(int32 QueueIndex)
{
	if (PlaybackQueue.IsValidIndex(QueueIndex))
	{
		PlaybackQueue.RemoveAt(QueueIndex);

		// Adjust PlaybackQueueIndex for removal
		if (PlaybackQueue.Num() == 0)
		{
			PlaybackQueueIndex = 0;
			PlaybackPosition = 0.0f;
			if (bIsPlaying) StopPlayback();
		}
		else if (QueueIndex < PlaybackQueueIndex)
		{
			// Entry removed before current — shift index back
			PlaybackQueueIndex--;
		}
		else if (QueueIndex == PlaybackQueueIndex)
		{
			// Removed the currently-playing entry — reset position
			PlaybackPosition = 0.0f;
			CachedPlaybackTiming = FFlipbookTimingData();
			if (PlaybackQueueIndex >= PlaybackQueue.Num())
			{
				// Was the last entry — stop playback
				if (bIsPlaying) StopPlayback();
				PlaybackQueueIndex = 0;
			}
		}

		RefreshPlaybackQueueList();
	}
}

void SCharacterDataAssetEditor::ClearPlaybackQueue()
{
	if (bIsPlaying) StopPlayback();
	PlaybackQueue.Empty();
	PlaybackQueueIndex = 0;
	PlaybackPosition = 0.0f;
	CachedPlaybackTiming = FFlipbookTimingData();
	RefreshPlaybackQueueList();
}

void SCharacterDataAssetEditor::ReorderQueueEntry(int32 FromIndex, int32 ToIndex)
{
	if (!PlaybackQueue.IsValidIndex(FromIndex) || !PlaybackQueue.IsValidIndex(ToIndex)) return;
	if (FromIndex == ToIndex) return;

	int32 MovedAnim = PlaybackQueue[FromIndex];
	PlaybackQueue.RemoveAt(FromIndex);

	int32 InsertAt = (ToIndex > FromIndex) ? ToIndex - 1 : ToIndex;
	PlaybackQueue.Insert(MovedAnim, InsertAt);

	// Track the currently-playing entry through the reorder
	if (bIsPlaying)
	{
		if (PlaybackQueueIndex == FromIndex)
		{
			PlaybackQueueIndex = InsertAt;
		}
		else
		{
			if (FromIndex < PlaybackQueueIndex && InsertAt >= PlaybackQueueIndex)
				PlaybackQueueIndex--;
			else if (FromIndex > PlaybackQueueIndex && InsertAt <= PlaybackQueueIndex)
				PlaybackQueueIndex++;
		}
	}

	RefreshPlaybackQueueList();
}

int32 SCharacterDataAssetEditor::GetAdjacentAnimationIndex(int32 Direction) const
{
	if (!Asset.IsValid() || Asset->Animations.Num() <= 1) return INDEX_NONE;

	// If queue is active, use queue order
	if (PlaybackQueue.Num() > 0)
	{
		// Find current animation in queue
		int32 CurrentQueuePos = INDEX_NONE;
		for (int32 i = 0; i < PlaybackQueue.Num(); i++)
		{
			if (PlaybackQueue[i] == SelectedAnimationIndex)
			{
				CurrentQueuePos = i;
				break;
			}
		}
		if (CurrentQueuePos != INDEX_NONE)
		{
			int32 TargetQueuePos = CurrentQueuePos + Direction;
			if (PlaybackQueue.IsValidIndex(TargetQueuePos))
			{
				return PlaybackQueue[TargetQueuePos];
			}
		}
		return INDEX_NONE;
	}

	// No queue — use animation list order, wrapping around
	int32 TargetIdx = SelectedAnimationIndex + Direction;
	if (TargetIdx < 0) TargetIdx = Asset->Animations.Num() - 1;
	else if (TargetIdx >= Asset->Animations.Num()) TargetIdx = 0;
	return TargetIdx;
}

void SCharacterDataAssetEditor::OnEditAlignmentClicked(int32 AnimationIndex)
{
	SelectedAnimationIndex = AnimationIndex;
	SelectedFrameIndex = 0;
	SwitchToTab(2);
}

void SCharacterDataAssetEditor::RefreshCurrentFrameFlipState()
{
	const FAnimationHitboxData* Anim = GetCurrentAnimation();
	if (!Anim || !Anim->FrameExtractionInfo.IsValidIndex(SelectedFrameIndex))
	{
		bSpriteFlipX = false;
		bSpriteFlipY = false;
		return;
	}

	bSpriteFlipX = Anim->FrameExtractionInfo[SelectedFrameIndex].bFlipX;
	bSpriteFlipY = Anim->FrameExtractionInfo[SelectedFrameIndex].bFlipY;
}

void SCharacterDataAssetEditor::OnApplyFlipToCurrentFrame()
{
	if (!Asset.IsValid()) return;
	FAnimationHitboxData* Anim = GetCurrentAnimationMutable();
	if (!Anim || !Anim->Frames.IsValidIndex(SelectedFrameIndex)) return;

	BeginTransaction(LOCTEXT("ApplyFlipCurrentFrameTxn", "Apply Sprite Flip to Current Frame"));
	Asset->SetSpriteFlipInRange(Anim->AnimationName, SelectedFrameIndex, SelectedFrameIndex, bSpriteFlipX, bSpriteFlipY);
	EndTransaction();
	RefreshCurrentFrameFlipState();
}

void SCharacterDataAssetEditor::OnApplyFlipToCurrentAnimation()
{
	if (!Asset.IsValid()) return;
	FAnimationHitboxData* Anim = GetCurrentAnimationMutable();
	if (!Anim || Anim->Frames.Num() == 0) return;

	BeginTransaction(LOCTEXT("ApplyFlipCurrentAnimationTxn", "Apply Sprite Flip to Animation"));
	Asset->SetSpriteFlipForAnimation(Anim->AnimationName, bSpriteFlipX, bSpriteFlipY);
	EndTransaction();
	RefreshCurrentFrameFlipState();
}

void SCharacterDataAssetEditor::OnApplyFlipToAllAnimations()
{
	if (!Asset.IsValid()) return;

	BeginTransaction(LOCTEXT("ApplyFlipAllAnimationsTxn", "Apply Sprite Flip to All Animations"));
	Asset->SetSpriteFlipForAllAnimations(bSpriteFlipX, bSpriteFlipY);
	EndTransaction();
	RefreshCurrentFrameFlipState();
}

void SCharacterDataAssetEditor::ReorderFrame(int32 FromIndex, int32 ToIndex)
{
	if (!Asset.IsValid()) return;
	FAnimationHitboxData* Anim = GetCurrentAnimationMutable();
	if (!Anim) return;
	if (FromIndex == ToIndex) return;
	if (!Anim->Frames.IsValidIndex(FromIndex) || !Anim->Frames.IsValidIndex(ToIndex)) return;

	BeginTransaction(LOCTEXT("ReorderFrameTrans", "Reorder Frame"));
	Asset->Modify();

	// Bubble-swap the frame from FromIndex to ToIndex in all parallel arrays
	auto BubbleSwap = [](auto& Array, int32 From, int32 To)
	{
		if (From < To)
		{
			for (int32 i = From; i < To; i++)
				Array.Swap(i, i + 1);
		}
		else
		{
			for (int32 i = From; i > To; i--)
				Array.Swap(i, i - 1);
		}
	};

	// Reorder hitbox frame data
	BubbleSwap(Anim->Frames, FromIndex, ToIndex);

	// Reorder extraction info — only when already populated, pad to match Frames length first
	if (Anim->FrameExtractionInfo.Num() > 0)
	{
		if (Anim->FrameExtractionInfo.Num() < Anim->Frames.Num())
		{
			Anim->FrameExtractionInfo.SetNum(Anim->Frames.Num());
		}
		BubbleSwap(Anim->FrameExtractionInfo, FromIndex, ToIndex);
	}

	// Reorder flipbook key frames
	if (!Anim->Flipbook.IsNull())
	{
		if (UPaperFlipbook* FB = Anim->Flipbook.LoadSynchronous())
		{
			FB->Modify();
			{
				FScopedFlipbookMutator Mutator(FB);
				if (Mutator.KeyFrames.IsValidIndex(FromIndex) && Mutator.KeyFrames.IsValidIndex(ToIndex))
				{
					BubbleSwap(Mutator.KeyFrames, FromIndex, ToIndex);
				}
			}
			FB->MarkPackageDirty();
		}
	}

	// Rename all frames to sequential indices
	for (int32 i = 0; i < Anim->Frames.Num(); i++)
	{
		Anim->Frames[i].FrameName = FString::Printf(TEXT("Frame_%d"), i);
	}

	EndTransaction();

	// Follow the moved frame
	SelectedFrameIndex = ToIndex;

	RefreshAlignmentFrameList();
	RefreshFrameList();
}

#undef LOCTEXT_NAMESPACE
