// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CharacterProfileAssetEditor.h"
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
#include "Widgets/SToolTip.h"

#define LOCTEXT_NAMESPACE "CharacterProfileAssetEditor"

namespace
{
int32 FindAlignmentFrameIndexBySourceIndex(const FFlipbookHitboxData& Anim, int32 SourceFrameIndex)
{
	for (int32 Index = 0; Index < Anim.FrameExtractionInfo.Num(); ++Index)
	{
		if (Anim.FrameExtractionInfo[Index].SourceFrameIndex == SourceFrameIndex)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}
}

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
	TFunction<void(const FPointerEvent&)> OnClickedFunc;
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
			if (OnClickedFunc) { OnClickedFunc(MouseEvent); }
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
// Handles both flipbook-list-to-queue drags and queue-reorder drags
// ---------------------------------------------------------------------------
class FQueueDragDropOp : public FDragDropOperation
{
public:
	DRAG_DROP_OPERATOR_TYPE(FQueueDragDropOp, FDragDropOperation)

	int32 FlipbookIndex = INDEX_NONE;    // Set for flipbook-list-to-queue drags
	int32 SourceQueueIndex = INDEX_NONE;  // Set for queue-reorder drags

	static TSharedRef<FQueueDragDropOp> NewFromFlipbookList(int32 InFlipbookIndex, const FString& AnimName)
	{
		TSharedRef<FQueueDragDropOp> Op = MakeShareable(new FQueueDragDropOp());
		Op->FlipbookIndex = InFlipbookIndex;
		Op->DefaultHoverText = FText::FromString(AnimName);
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

	bool IsFromFlipbookList() const { return FlipbookIndex != INDEX_NONE && SourceQueueIndex == INDEX_NONE; }
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
	TFunction<void(int32, int32)> OnAnimDroppedFunc;     // (FlipbookIndex, InsertAtQueueIdx)

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
			else if (Op->IsFromFlipbookList() && OnAnimDroppedFunc)
			{
				OnAnimDroppedFunc(Op->FlipbookIndex, QueueIndex);
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
// Widget wrapper that enables drag-from-flipbook-list into queue
// ---------------------------------------------------------------------------
class SFlipbookListDragWrapper : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SFlipbookListDragWrapper) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	int32 FlipbookIndex = INDEX_NONE;
	FString FlipbookName;
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
					.BeginDragDrop(FQueueDragDropOp::NewFromFlipbookList(FlipbookIndex, FlipbookName));
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

TSharedRef<SWidget> SCharacterProfileAssetEditor::BuildAlignmentEditorTab()
{
	TSharedRef<SWidget> TabContent = SNew(SVerticalBox)

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

			// Left panel: Flipbook and Frame lists
			+ SSplitter::Slot()
			.Value(AlignmentSplitterLeftRatio)
			.OnSlotResized(SSplitter::FOnSlotResized::CreateLambda([this](float NewSize)
			{
				AlignmentSplitterLeftRatio = NewSize;
				SaveFloatLayoutValue(TEXT("AlignmentSplitterLeft"), NewSize);
			}))
			[
				SAssignNew(AlignmentLeftSectionsBox, SVerticalBox)
			]

			// Center: Canvas
			+ SSplitter::Slot()
			.Value(AlignmentSplitterCenterRatio)
			.OnSlotResized(SSplitter::FOnSlotResized::CreateLambda([this](float NewSize)
			{
				AlignmentSplitterCenterRatio = NewSize;
				SaveFloatLayoutValue(TEXT("AlignmentSplitterCenter"), NewSize);
			}))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					WrapWithActivePanelHighlight(FName(TEXT("Alignment.Canvas")), 2, BuildAlignmentCanvasArea())
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(4, 0, 4, 4)
				[
					WrapWithActivePanelHighlight(FName(TEXT("Alignment.BottomFrames")), 4,
						SNew(SBox)
						.HeightOverride(142.0f)
						[
							BuildAlignmentFrameList()
						]
					)
				]
			]

			// Right panel: Offset controls
			+ SSplitter::Slot()
			.Value(AlignmentSplitterRightRatio)
			.OnSlotResized(SSplitter::FOnSlotResized::CreateLambda([this](float NewSize)
			{
				AlignmentSplitterRightRatio = NewSize;
				SaveFloatLayoutValue(TEXT("AlignmentSplitterRight"), NewSize);
			}))
			[
				WrapWithActivePanelHighlight(FName(TEXT("Alignment.OffsetControls")), 4,
					SNew(SScrollBox)
					+ SScrollBox::Slot()
					[
						BuildOffsetControlsPanel()
					]
				)
			]
		];

	RebuildAlignmentLeftSections();
	return TabContent;
}

void SCharacterProfileAssetEditor::RebuildAlignmentLeftSections()
{
	if (!AlignmentLeftSectionsBox.IsValid())
	{
		return;
	}

	AlignmentLeftSectionsBox->ClearChildren();

	auto AddSection = [this](FName SectionId, const FText& SectionTitle, float FillWeight, TSharedRef<SWidget> SectionContent)
	{
		AlignmentLeftSectionsBox->AddSlot()
		.FillHeight(FillWeight)
		[
			BuildReorderableSectionCard(
				FName(*FString::Printf(TEXT("Alignment.Left.%s"), *SectionId.ToString())),
				SectionTitle,
				LOCTEXT("AlignmentSectionTooltip", "Sprite/Flipbook section"),
				SectionContent,
				true)
		];
	};

	for (const FName& SectionId : AlignmentLeftSectionOrder)
	{
		if (SectionId == FName(TEXT("Flipbooks")))
		{
			AddSection(
				SectionId,
				LOCTEXT("AlignmentSectionFlipbooks", "Flipbooks"),
				0.35f,
				BuildAlignmentFlipbookList());
		}
		else if (SectionId == FName(TEXT("Queue")))
		{
			AddSection(
				SectionId,
				LOCTEXT("AlignmentSectionQueue", "Playback Queue"),
				0.25f,
				BuildPlaybackQueuePanel());
		}
	}

	RefreshAlignmentFlipbookList();
	RefreshPlaybackQueueList();
	RefreshAlignmentFrameList();
}

TSharedRef<SWidget> SCharacterProfileAssetEditor::BuildAlignmentToolbar()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(FMargin(8, 4))
		[
			SNew(SHorizontalBox)

			// === Playback Controls ===
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 0, 4, 0)
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

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 0, 8, 0)
			[
				SNew(SCheckBox)
				.ToolTipText(LOCTEXT("PingPongTooltip", "Toggle Ping-Pong Playback (P)"))
				.IsChecked_Lambda([this]() { return bPingPongPlayback ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State) {
					bPingPongPlayback = (State == ECheckBoxState::Checked);
					if (!bPingPongPlayback) { bPlaybackReversed = false; }
				})
				[
					SNew(STextBlock)
					.Text(LOCTEXT("PingPongP", "Ping-Pong (P)"))
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
			.Padding(0, 0, 4, 0)
			[
				SNew(SCheckBox)
				.ToolTipText(LOCTEXT("OnionTooltip", "Toggle Backward Onion Skin (O)"))
				.IsChecked_Lambda([this]() { return bShowOnionSkin ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { bShowOnionSkin = (State == ECheckBoxState::Checked); })
				[
					SNew(STextBlock)
					.Text(LOCTEXT("OnionSkinO", "Onion (O)"))
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 0, 8, 0)
			[
				SNew(SCheckBox)
				.ToolTipText(LOCTEXT("ForwardOnionTooltip", "Toggle Forward Onion Skin (F)"))
				.IsChecked_Lambda([this]() { return bShowForwardOnionSkin ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { bShowForwardOnionSkin = (State == ECheckBoxState::Checked); })
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ForwardOnionF", "Forward (F)"))
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
					.IsEnabled_Lambda([this]() { return bShowOnionSkin || bShowForwardOnionSkin; })
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
					.IsEnabled_Lambda([this]() { return bShowOnionSkin || bShowForwardOnionSkin; })
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
				.ToolTipText(LOCTEXT("RefSpriteTooltip", "Toggle Reference Sprite overlay (R). Shows a persistent reference frame from any flipbook for alignment comparison."))
				.IsChecked_Lambda([this]() { return bShowReferenceSprite && ReferenceFlipbookIndex != INDEX_NONE ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State) {
					if (State == ECheckBoxState::Checked)
					{
						if (ReferenceFlipbookIndex == INDEX_NONE)
						{
							// No reference set yet — capture current frame
							SetReferenceSprite(SelectedFlipbookIndex, SelectedFrameIndex);
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
						if (bShowReferenceSprite && ReferenceFlipbookIndex != INDEX_NONE && Asset.IsValid()
							&& Asset->Flipbooks.IsValidIndex(ReferenceFlipbookIndex))
						{
							return FText::Format(LOCTEXT("RefLabelActive", "Ref: {0} #{1}"),
								FText::FromString(Asset->Flipbooks[ReferenceFlipbookIndex].FlipbookName),
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
					.IsEnabled_Lambda([this]() { return bShowReferenceSprite && ReferenceFlipbookIndex != INDEX_NONE; })
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 0, 8, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.ToolTipText(LOCTEXT("ClearRefTooltip", "Clear reference sprite"))
				.IsEnabled_Lambda([this]() { return ReferenceFlipbookIndex != INDEX_NONE; })
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
						MenuBuilder.AddSeparator();
						AddAnchorOption(ESpriteAnchor::None, LOCTEXT("AnchorNone", "None (Hide Reticle)"));

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
								case ESpriteAnchor::None:         return LOCTEXT("AN", "None");
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

TSharedRef<SWidget> SCharacterProfileAssetEditor::BuildAlignmentFlipbookList()
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
			SAssignNew(AlignmentFlipbookSearchBox, SSearchBox)
			.HintText(LOCTEXT("SearchFlipbooks", "Search..."))
			.OnTextChanged_Lambda([this](const FText& NewText) {
				AlignmentFlipbookSearchFilter = NewText.ToString();

				// Debounce: cancel previous timer, start new one
				if (AlignmentFlipbookSearchDebounceTimer.IsValid())
				{
					UnRegisterActiveTimer(AlignmentFlipbookSearchDebounceTimer.Pin().ToSharedRef());
				}
				AlignmentFlipbookSearchDebounceTimer = RegisterActiveTimer(0.2f,
					FWidgetActiveTimerDelegate::CreateLambda(
						[this](double, float) {
							RefreshAlignmentFlipbookList();
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
				SAssignNew(AlignmentFlipbookListBox, SVerticalBox)
			]
		];
}

TSharedRef<SWidget> SCharacterProfileAssetEditor::BuildAlignmentFrameList()
{
	SAssignNew(AlignmentFrameListBox, SHorizontalBox);

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
				.Text(LOCTEXT("AlignFrames", "FRAMES"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 6, 0)
			[
				SNew(STextBlock)
				.Text_Lambda([this]() {
					const int32 ExcludedCount = Asset.IsValid()
						? Asset->GetExcludedFlipbookFrameCount(SelectedFlipbookIndex)
						: 0;
					return ExcludedCount > 0
						? FText::Format(LOCTEXT("ExcludedCountLabel", "Excluded: {0}"), FText::AsNumber(ExcludedCount))
						: FText::GetEmpty();
				})
				.ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f)))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SComboButton)
				.ToolTipText(LOCTEXT("RestoreExcludedAlignmentTooltip", "Restore excluded frames back into this flipbook"))
				.OnGetMenuContent_Lambda([this]() { return BuildAlignmentRestoreExcludedMenu(); })
				.IsEnabled_Lambda([this]() {
					return Asset.IsValid() && Asset->GetExcludedFlipbookFrameCount(SelectedFlipbookIndex) > 0;
				})
				.ButtonContent()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("RestoreExcludedFramesShort", "Restore"))
				]
			]
		]

		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SScrollBox)
			.Orientation(Orient_Horizontal)
			.ScrollBarAlwaysVisible(true)
			+ SScrollBox::Slot()
			[
				AlignmentFrameListBox.ToSharedRef()
			]
		];
}

TSharedRef<SWidget> SCharacterProfileAssetEditor::BuildPlaybackQueuePanel()
{
	TSharedPtr<SQueueEntryDragDropWrapper> EmptyQueueDropTarget;

	TSharedRef<SWidget> Panel = SNew(SVerticalBox)

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

			// Empty state: full-area drop target (accepts flipbook drags when queue is empty)
			+ SOverlay::Slot()
			[
				SAssignNew(EmptyQueueDropTarget, SQueueEntryDragDropWrapper)
				.Visibility_Lambda([this]() {
					return PlaybackQueue.Num() == 0 ? EVisibility::Visible : EVisibility::Collapsed;
				})
				[
					SNew(SBox)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("DragHint", "Drag flipbooks here"))
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					]
				]
			]
		];

	// Wire up the empty-state drop target
	if (EmptyQueueDropTarget.IsValid())
	{
		EmptyQueueDropTarget->QueueIndex = 0;
		EmptyQueueDropTarget->OnAnimDroppedFunc = [this](int32 FlipbookIndex, int32 InsertAt) {
			if (Asset.IsValid() && Asset->Flipbooks.IsValidIndex(FlipbookIndex))
			{
				PlaybackQueue.Add(FlipbookIndex);
				RefreshPlaybackQueueList();
			}
		};
	}

	return Panel;
}

TSharedRef<SWidget> SCharacterProfileAssetEditor::BuildAlignmentCanvasArea()
{
	TSharedRef<SBorder> Border = SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(0)
		[
			SAssignNew(AlignmentCanvas, SSpriteAlignmentCanvas)
			.Asset(Asset.Get())
			.SelectedFlipbookIndex_Lambda([this]() { return SelectedFlipbookIndex; })
			.SelectedFrameIndex_Lambda([this]() { return SelectedFrameIndex; })
			.ShowGrid_Lambda([this]() { return bShowAlignmentGrid; })
			.Zoom_Lambda([this]() { return AlignmentZoomLevel; })
			.ShowOnionSkin_Lambda([this]() { return bShowOnionSkin; })
			.OnionSkinFrames_Lambda([this]() { return OnionSkinFrames; })
			.OnionSkinOpacity_Lambda([this]() { return OnionSkinOpacity; })
			.PreviousFlipbookIndex_Lambda([this]() { return GetAdjacentFlipbookIndex(-1); })
			.ShowForwardOnionSkin_Lambda([this]() { return bShowForwardOnionSkin; })
			.NextFlipbookIndex_Lambda([this]() { return GetAdjacentFlipbookIndex(1); })
			.ShowReticle_Lambda([this]() { return AlignmentReticleAnchor != ESpriteAnchor::None; })
			.ReticleAnchor_Lambda([this]() { return AlignmentReticleAnchor; })
			.FlipX_Lambda([this]() { return bSpriteFlipX; })
			.FlipY_Lambda([this]() { return bSpriteFlipY; })
			.ShowReferenceSprite_Lambda([this]() { return bShowReferenceSprite && ReferenceFlipbookIndex != INDEX_NONE; })
			.ReferenceSprite_Lambda([this]() -> TWeakObjectPtr<UPaperSprite> {
				if (!bShowReferenceSprite || !Asset.IsValid()) return nullptr;
				if (!Asset->Flipbooks.IsValidIndex(ReferenceFlipbookIndex)) return nullptr;
				const FFlipbookHitboxData& Anim = Asset->Flipbooks[ReferenceFlipbookIndex];
				if (Anim.Flipbook.IsNull()) return nullptr;
				UPaperFlipbook* FB = Anim.Flipbook.LoadSynchronous();
				if (!FB || ReferenceFrameIndex >= FB->GetNumKeyFrames()) return nullptr;
				return FB->GetKeyFrameChecked(ReferenceFrameIndex).Sprite;
			})
			.ReferenceSpriteOffset_Lambda([this]() -> FIntPoint {
				if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(ReferenceFlipbookIndex)) return FIntPoint::ZeroValue;
				const FFlipbookHitboxData& Anim = Asset->Flipbooks[ReferenceFlipbookIndex];
				if (!Anim.FrameExtractionInfo.IsValidIndex(ReferenceFrameIndex)) return FIntPoint::ZeroValue;
				return Anim.FrameExtractionInfo[ReferenceFrameIndex].SpriteOffset;
			})
			.ReferenceSpriteOpacity_Lambda([this]() { return ReferenceSpriteOpacity; })
			.QueueLargestDims_Lambda([this]() -> FIntPoint {
				if (PlaybackQueue.Num() < 2 || !Asset.IsValid())
				{
					return FIntPoint::ZeroValue;
				}
				FIntPoint Largest(1, 1);
				for (int32 Idx : PlaybackQueue)
				{
					if (!Asset->Flipbooks.IsValidIndex(Idx)) continue;
					const FFlipbookHitboxData& FBData = Asset->Flipbooks[Idx];
					if (FBData.Flipbook.IsNull()) continue;
					UPaperFlipbook* FB = FBData.Flipbook.Get();
					if (!FB) continue;
					for (int32 i = 0; i < FB->GetNumKeyFrames(); ++i)
					{
						if (UPaperSprite* S = FB->GetKeyFrameChecked(i).Sprite)
						{
							FVector2D Sz = S->GetSourceSize();
							Largest.X = FMath::Max(Largest.X, FMath::RoundToInt(Sz.X));
							Largest.Y = FMath::Max(Largest.Y, FMath::RoundToInt(Sz.Y));
						}
					}
				}
				return Largest;
			})
		];

	// Wire up canvas delegates
	if (AlignmentCanvas.IsValid())
	{
		AlignmentCanvas->OnOffsetChanged.BindSP(this, &SCharacterProfileAssetEditor::OnAlignmentOffsetChanged);
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

TSharedRef<SWidget> SCharacterProfileAssetEditor::BuildOffsetControlsPanel()
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
							const FFlipbookHitboxData* Anim = GetCurrentFlipbookData();
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
				.Padding(0, 0, 0, 4)
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
							const FFlipbookHitboxData* Anim = GetCurrentFlipbookData();
							if (Anim && Anim->FrameExtractionInfo.IsValidIndex(SelectedFrameIndex))
							{
								return Anim->FrameExtractionInfo[SelectedFrameIndex].SpriteOffset.Y;
							}
							return 0;
						})
						.OnValueChanged_Lambda([this](int32 NewValue) { OnOffsetYChanged(NewValue); })
					]
				]

				// Reset offset
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
		.Padding(0, 0, 0, 4)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.HAlign(HAlign_Center)
			.IsEnabled_Lambda([this]() { return SelectedFrames.Num() > 0; })
			.ToolTipText(LOCTEXT("ApplyToSelectedTooltip", "Apply current offset to selected frames (Ctrl/Shift+Click in frame strip)"))
			.OnClicked_Lambda([this]()
			{
				FFlipbookHitboxData* Anim = GetCurrentFlipbookDataMutable();
				if (!Anim) return FReply::Handled();
				int32 FrameCount = GetCurrentFrameCount();
				if (FrameCount <= 0) return FReply::Handled();
				if (Anim->FrameExtractionInfo.Num() < FrameCount)
				{
					Anim->FrameExtractionInfo.SetNum(FrameCount);
				}
				if (!Anim->FrameExtractionInfo.IsValidIndex(SelectedFrameIndex)) return FReply::Handled();
				FIntPoint CurrentOffset = Anim->FrameExtractionInfo[SelectedFrameIndex].SpriteOffset;
				BeginTransaction(LOCTEXT("ApplyOffsetSelected", "Apply Offset to Selected Frames"));
				Asset->Modify();
				ForEachSelectedFrame([&](int32 Idx)
				{
					if (Anim->FrameExtractionInfo.IsValidIndex(Idx))
					{
						Anim->FrameExtractionInfo[Idx].SpriteOffset = CurrentOffset;
						Anim->FrameExtractionInfo[Idx].bHasCustomAlignment = true;
					}
				});
				EndTransaction();
				RefreshAlignmentFrameList();
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ApplyToSelected", "Apply to Selected"))
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
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.HAlign(HAlign_Center)
			.Text(LOCTEXT("ApplyOffsets", "Apply Offsets to Flipbook"))
			.ToolTipText(LOCTEXT("ApplyOffsetsTooltip", "Apply alignment offsets to the actual sprite assets by shifting their SourceUV. Offsets will be reset to zero after applying."))
			.ButtonColorAndOpacity_Lambda([this]() -> FLinearColor {
				if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex))
					return FLinearColor::White;
				const FFlipbookHitboxData& Anim = Asset->Flipbooks[SelectedFlipbookIndex];
				for (const FSpriteExtractionInfo& Info : Anim.FrameExtractionInfo)
				{
					if (Info.SpriteOffset != FIntPoint::ZeroValue)
						return FLinearColor(0.2f, 0.8f, 0.3f);
				}
				return FLinearColor::White;
			})
			.IsEnabled_Lambda([this]()
			{
				if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex)) return false;
				const FFlipbookHitboxData& Anim = Asset->Flipbooks[SelectedFlipbookIndex];
				for (const FSpriteExtractionInfo& Info : Anim.FrameExtractionInfo)
				{
					if (Info.SpriteOffset != FIntPoint::ZeroValue) return true;
				}
				return false;
			})
			.OnClicked_Lambda([this]() -> FReply
			{
				if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex))
				{
					return FReply::Handled();
				}

				FFlipbookHitboxData& Anim = Asset->Flipbooks[SelectedFlipbookIndex];
				UPaperFlipbook* Flipbook = Anim.Flipbook.LoadSynchronous();
				if (!Flipbook)
				{
					FNotificationInfo Notification(LOCTEXT("NoFlipbookForOffsets", "No flipbook assigned. Set a flipbook reference first."));
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

				// Invalidate cached dims so reticle/grid reposition to account for new pivots
				if (AlignmentCanvas.IsValid())
				{
					AlignmentCanvas->InvalidateCachedDims();
				}

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

		// Spacer
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNullWidget::NullWidget
		];
}

void SCharacterProfileAssetEditor::RefreshAlignmentFlipbookList()
{
	if (!AlignmentFlipbookListBox.IsValid() || !Asset.IsValid()) return;

	AlignmentFlipbookListBox->ClearChildren();
	AlignmentFlipbookNameTexts.Empty();

	// Filter for search
	TFunction<bool(int32)> SearchFilter = nullptr;
	if (!AlignmentFlipbookSearchFilter.IsEmpty())
	{
		SearchFilter = [this](int32 Idx) -> bool
		{
			return Asset->Flipbooks[Idx].FlipbookName.Contains(AlignmentFlipbookSearchFilter, ESearchCase::IgnoreCase);
		};
	}

	BuildGroupedFlipbookList(AlignmentFlipbookListBox, [this](int32 i) -> TSharedRef<SWidget>
	{
		const FFlipbookHitboxData& Anim = Asset->Flipbooks[i];
		const bool bIsSelected = (i == SelectedFlipbookIndex);
		UPaperFlipbook* LoadedFlipbook = !Anim.Flipbook.IsNull() ? Anim.Flipbook.LoadSynchronous() : nullptr;
		const bool bHasFlipbook = LoadedFlipbook != nullptr;
		const int32 FrameCount = bHasFlipbook ? LoadedFlipbook->GetNumKeyFrames() : Anim.Frames.Num();
		const FText SourceNameText = FText::FromString(bHasFlipbook ? Anim.Flipbook.GetAssetName() : TEXT("No Flipbook Assigned"));

		TSharedPtr<SInlineEditableTextBlock> NameText;
		TSharedPtr<SFlipbookListDragWrapper> Wrapper;

		TSharedRef<SWidget> Item = SAssignNew(Wrapper, SFlipbookListDragWrapper)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("NoBorder"))
				.Padding(2)
				.ToolTip(
					SNew(SToolTip)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0, 0, 0, 6)
						[
							SNew(STextBlock)
							.Text(FText::FromString(Anim.FlipbookName))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SBox)
							.WidthOverride(128)
							.HeightOverride(128)
							[
								bHasFlipbook
									? StaticCastSharedRef<SWidget>(SNew(SFlipbookThumbnail).Flipbook(LoadedFlipbook))
									: StaticCastSharedRef<SWidget>(
										SNew(SBorder)
										.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
										.HAlign(HAlign_Center)
										.VAlign(VAlign_Center)
										[
											SNew(STextBlock)
											.Text(LOCTEXT("NoFlipbookTooltipPreview", "No FB"))
											.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
											.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
										])
							]
						]
					])
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("NoBorder"))
					[
						SNew(SBorder)
						.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
						.BorderBackgroundColor(bIsSelected
							? FLinearColor(0.15f, 0.35f, 0.55f, 1.0f)
							: FLinearColor(0.03f, 0.03f, 0.03f, 1.0f))
						.Padding(FMargin(8, 6))
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(0, 0, 8, 0)
							[
								SNew(SBox)
								.WidthOverride(44)
								.HeightOverride(44)
								[
									bHasFlipbook
										? StaticCastSharedRef<SWidget>(SNew(SFlipbookThumbnail).Flipbook(LoadedFlipbook))
										: StaticCastSharedRef<SWidget>(
											SNew(SBorder)
											.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
											.HAlign(HAlign_Center)
											.VAlign(VAlign_Center)
											[
												SNew(STextBlock)
												.Text(LOCTEXT("NoFlipbookListPreview", "No FB"))
												.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
												.ColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.55f, 0.55f)))
											])
								]
							]
							+ SHorizontalBox::Slot()
							.FillWidth(1.0f)
							.VAlign(VAlign_Center)
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot()
								.AutoHeight()
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot()
									.FillWidth(1.0f)
									.VAlign(VAlign_Center)
									[
										SAssignNew(NameText, SInlineEditableTextBlock)
										.Text(FText::FromString(Anim.FlipbookName))
										.OnTextCommitted_Lambda([this, i](const FText& NewText, ETextCommit::Type CommitType)
										{
											if (CommitType != ETextCommit::OnCleared)
											{
												RenameFlipbook(i, NewText.ToString());
											}
										})
									]
									+ SHorizontalBox::Slot()
									.AutoWidth()
									.VAlign(VAlign_Center)
									.Padding(6, 0, 0, 0)
									[
										SNew(STextBlock)
										.Text(FText::Format(LOCTEXT("AlignmentFrameCount", "{0} frames"), FText::AsNumber(FrameCount)))
										.Font(FAppStyle::GetFontStyle("SmallFont"))
										.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
									]
									+ SHorizontalBox::Slot()
									.AutoWidth()
									.VAlign(VAlign_Center)
									.Padding(6, 0, 0, 0)
									[
										SNew(STextBlock)
										.Text_Lambda([this, i]() -> FText
										{
											if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(i)) return FText::GetEmpty();
											for (const FSpriteExtractionInfo& Info : Asset->Flipbooks[i].FrameExtractionInfo)
											{
												if (Info.SpriteOffset != FIntPoint::ZeroValue) return LOCTEXT("UnappliedMark", "*");
											}
											return FText::GetEmpty();
										})
										.ColorAndOpacity(FLinearColor::Yellow)
									]
								]
								+ SVerticalBox::Slot()
								.AutoHeight()
								.Padding(0, 2, 0, 0)
								[
									SNew(STextBlock)
									.Text(SourceNameText)
									.Font(FAppStyle::GetFontStyle("SmallFont"))
									.ColorAndOpacity(bHasFlipbook ? FLinearColor(0.4f, 0.8f, 0.4f) : FLinearColor(0.6f, 0.4f, 0.4f))
								]
							]
						]
					]
				]
			];

		Wrapper->FlipbookIndex = i;
		Wrapper->FlipbookName = Anim.FlipbookName;
		Wrapper->OnClickedFunc = [this, i]() {
			OnFlipbookSelected(i);
			RefreshAlignmentFrameList();
		};
		Wrapper->OnRightClickFunc = [this, i](const FGeometry&, const FPointerEvent&) {
			OnFlipbookSelected(i);
			RefreshAlignmentFrameList();
			ShowFlipbookContextMenu(i);
		};

		AlignmentFlipbookNameTexts.Add(i, NameText);
		return Item;
	}, SearchFilter);

	// Trigger pending rename
	TriggerPendingRenameIfNeeded(AlignmentFlipbookNameTexts);
}

void SCharacterProfileAssetEditor::RefreshPlaybackQueueList()
{
	if (!PlaybackQueueListBox.IsValid() || !Asset.IsValid()) return;

	bool bPurgedInvalidEntries = false;
	for (int32 i = PlaybackQueue.Num() - 1; i >= 0; --i)
	{
		if (!Asset->Flipbooks.IsValidIndex(PlaybackQueue[i]))
		{
			if (i < PlaybackQueueIndex)
			{
				PlaybackQueueIndex--;
			}
			else if (i == PlaybackQueueIndex)
			{
				PlaybackPosition = 0.0f;
				CachedPlaybackTiming = FFlipbookTimingData();
			}
			PlaybackQueue.RemoveAt(i);
			bPurgedInvalidEntries = true;
		}
	}

	if (bPurgedInvalidEntries)
	{
		if (bIsPlaying)
		{
			StopPlayback();
			return;
		}
		if (PlaybackQueue.Num() == 0)
		{
			PlaybackQueueIndex = 0;
			PlaybackPosition = 0.0f;
			CachedPlaybackTiming = FFlipbookTimingData();
		}
		else
		{
			PlaybackQueueIndex = FMath::Clamp(PlaybackQueueIndex, 0, PlaybackQueue.Num() - 1);
		}
	}

	PlaybackQueueListBox->ClearChildren();

	for (int32 QueueIdx = 0; QueueIdx < PlaybackQueue.Num(); QueueIdx++)
	{
		int32 AnimIdx = PlaybackQueue[QueueIdx];

		const FFlipbookHitboxData& Anim = Asset->Flipbooks[AnimIdx];
		bool bIsActive = bIsPlaying && QueueIdx == PlaybackQueueIndex;
		bool bIsCurrentSelection = (AnimIdx == SelectedFlipbookIndex);
		UPaperFlipbook* LoadedFlipbook = !Anim.Flipbook.IsNull() ? Anim.Flipbook.LoadSynchronous() : nullptr;
		const bool bHasFlipbook = LoadedFlipbook != nullptr;
		const int32 FrameCount = bHasFlipbook ? LoadedFlipbook->GetNumKeyFrames() : Anim.Frames.Num();

		// Background color: green for actively playing, blue for selected, dark default
		FLinearColor BgColor = FLinearColor(0.03f, 0.03f, 0.03f, 1.0f);
		if (bIsActive)
		{
			BgColor = FLinearColor(0.1f, 0.35f, 0.1f, 1.0f);
		}
		else if (bIsCurrentSelection)
		{
			BgColor = FLinearColor(0.15f, 0.35f, 0.55f, 1.0f);
		}

		TSharedPtr<SQueueEntryDragDropWrapper> Wrapper;

		PlaybackQueueListBox->AddSlot()
		.AutoHeight()
		.Padding(0, 0, 0, 2)
		[
			SAssignNew(Wrapper, SQueueEntryDragDropWrapper)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				.BorderBackgroundColor(BgColor)
				.Padding(FMargin(8, 6))
				[
					SNew(SHorizontalBox)

					// Queue index number
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0, 0, 6, 0)
					[
						SNew(STextBlock)
						.Text(FText::AsNumber(QueueIdx + 1))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.4f, 0.4f, 0.4f)))
					]

					// Flipbook thumbnail
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0, 0, 8, 0)
					[
						SNew(SBox)
						.WidthOverride(36)
						.HeightOverride(36)
						[
							bHasFlipbook
								? StaticCastSharedRef<SWidget>(SNew(SFlipbookThumbnail).Flipbook(LoadedFlipbook))
								: StaticCastSharedRef<SWidget>(
									SNew(SBorder)
									.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
									.HAlign(HAlign_Center)
									.VAlign(VAlign_Center)
									[
										SNew(STextBlock)
										.Text(LOCTEXT("NoFlipbookQueuePreview", "?"))
										.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
										.ColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.55f, 0.55f)))
									])
						]
					]

					// Name + frame count
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(STextBlock)
							.Text(FText::FromString(Anim.FlipbookName))
							.Font(bIsActive ? FCoreStyle::GetDefaultFontStyle("Bold", 9) : FCoreStyle::GetDefaultFontStyle("Regular", 9))
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0, 1, 0, 0)
						[
							SNew(STextBlock)
							.Text(FText::Format(LOCTEXT("QueueFrameCount", "{0} frames"), FText::AsNumber(FrameCount)))
							.Font(FAppStyle::GetFontStyle("SmallFont"))
							.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
						]
					]

					// Remove button
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "SimpleButton")
						.ToolTipText(LOCTEXT("RemoveFromQueue", "Remove from queue"))
						.OnClicked_Lambda([this, QueueIdx]() {
							RemoveFromPlaybackQueue(QueueIdx);
							return FReply::Handled();
						})
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("X")))
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
							.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
						]
					]
				]
			]
		];

		Wrapper->QueueIndex = QueueIdx;
		Wrapper->FlipbookIndex = AnimIdx;
		Wrapper->FlipbookName = Anim.FlipbookName;
		Wrapper->OnClickedFunc = [this, AnimIdx]() {
			OnFlipbookSelected(AnimIdx);
			RefreshAlignmentFlipbookList();
			RefreshAlignmentFrameList();
		};
		Wrapper->OnRightClickFunc = [this, QueueIdx, AnimIdx]() {
			FMenuBuilder MenuBuilder(true, nullptr);

			MenuBuilder.AddMenuEntry(
				LOCTEXT("QueueOpenFlipbookAsset", "Open Flipbook Asset"),
				LOCTEXT("QueueOpenFlipbookAssetTooltip", "Open this queued flipbook asset in its editor"),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateLambda([this, AnimIdx]() { OpenFlipbookAssetEditor(AnimIdx); }),
					FCanExecuteAction::CreateLambda([this, AnimIdx]() { return GetFlipbookAssetForIndex(AnimIdx) != nullptr; })
				)
			);

			MenuBuilder.AddMenuEntry(
				LOCTEXT("QueueBrowseToFlipbookAsset", "Browse to Flipbook in Content Browser"),
				LOCTEXT("QueueBrowseToFlipbookAssetTooltip", "Sync the Content Browser to this queued flipbook asset"),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateLambda([this, AnimIdx]() { BrowseToFlipbookAssetInContentBrowser(AnimIdx); }),
					FCanExecuteAction::CreateLambda([this, AnimIdx]() { return GetFlipbookAssetForIndex(AnimIdx) != nullptr; })
				)
			);

			MenuBuilder.AddMenuSeparator();

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
					FExecuteAction::CreateLambda([this, QueueIdx]() { ReorderQueueEntry(QueueIdx, QueueIdx + 2); }),
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
		Wrapper->OnAnimDroppedFunc = [this](int32 FlipbookIndex, int32 InsertAt) {
			// Insert flipbook at this position in the queue
			if (Asset.IsValid() && Asset->Flipbooks.IsValidIndex(FlipbookIndex))
			{
				PlaybackQueue.Insert(FlipbookIndex, InsertAt);
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
						.Text(FText::GetEmpty())
						.Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
					]
				]
			]
		];

		DropTarget->QueueIndex = PlaybackQueue.Num();
		DropTarget->OnAnimDroppedFunc = [this](int32 FlipbookIndex, int32 InsertAt) {
			if (Asset.IsValid() && Asset->Flipbooks.IsValidIndex(FlipbookIndex))
			{
				PlaybackQueue.Insert(FlipbookIndex, InsertAt);
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

void SCharacterProfileAssetEditor::RefreshAlignmentFrameList()
{
	if (!AlignmentFrameListBox.IsValid() || !Asset.IsValid()) return;

	AlignmentFrameListBox->ClearChildren();

	const FFlipbookHitboxData* Anim = GetCurrentFlipbookData();
	if (!Anim) return;

	// Get the live flipbook for active frame sprites.
	UPaperFlipbook* Flipbook = nullptr;
	if (!Anim->Flipbook.IsNull())
	{
		Flipbook = Anim->Flipbook.LoadSynchronous();
	}

	const int32 ActiveFrameCount = FMath::Min(GetCurrentFrameCount(), Anim->Frames.Num());
	if (ActiveFrameCount > 0)
	{
		SelectedFrameIndex = FMath::Clamp(SelectedFrameIndex, 0, ActiveFrameCount - 1);
	}
	else
	{
		SelectedFrameIndex = 0;
	}

	struct FDisplayedAlignmentFrameEntry
	{
		bool bExcluded = false;
		int32 SourceFrameIndex = INDEX_NONE;
		int32 ActiveFrameIndex = INDEX_NONE;
		int32 ExcludedFrameIndex = INDEX_NONE;
		const FFrameHitboxData* FrameData = nullptr;
		const FSpriteExtractionInfo* ExtractionInfo = nullptr;
		UPaperSprite* Sprite = nullptr;
	};

	TArray<FDisplayedAlignmentFrameEntry> DisplayEntries;
	DisplayEntries.Reserve(ActiveFrameCount + Anim->ExcludedFrames.Num());

	for (int32 ActiveIndex = 0; ActiveIndex < ActiveFrameCount; ++ActiveIndex)
	{
		const FSpriteExtractionInfo* ExtractionInfo = Anim->FrameExtractionInfo.IsValidIndex(ActiveIndex)
			? &Anim->FrameExtractionInfo[ActiveIndex]
			: nullptr;
		const int32 SourceIndex = (ExtractionInfo && ExtractionInfo->SourceFrameIndex != INDEX_NONE)
			? ExtractionInfo->SourceFrameIndex
			: ActiveIndex;

		UPaperSprite* Sprite = nullptr;
		if (Flipbook && ActiveIndex < Flipbook->GetNumKeyFrames())
		{
			Sprite = Flipbook->GetKeyFrameChecked(ActiveIndex).Sprite;
		}

		FDisplayedAlignmentFrameEntry& Entry = DisplayEntries.AddDefaulted_GetRef();
		Entry.bExcluded = false;
		Entry.SourceFrameIndex = SourceIndex;
		Entry.ActiveFrameIndex = ActiveIndex;
		Entry.FrameData = &Anim->Frames[ActiveIndex];
		Entry.ExtractionInfo = ExtractionInfo;
		Entry.Sprite = Sprite;
	}

	for (int32 ExcludedIndex = 0; ExcludedIndex < Anim->ExcludedFrames.Num(); ++ExcludedIndex)
	{
		const FExcludedFlipbookFrameData& ExcludedFrame = Anim->ExcludedFrames[ExcludedIndex];
		const int32 SourceIndex = ExcludedFrame.ExtractionInfo.SourceFrameIndex != INDEX_NONE
			? ExcludedFrame.ExtractionInfo.SourceFrameIndex
			: (ActiveFrameCount + ExcludedIndex);

		FDisplayedAlignmentFrameEntry& Entry = DisplayEntries.AddDefaulted_GetRef();
		Entry.bExcluded = true;
		Entry.SourceFrameIndex = SourceIndex;
		Entry.ExcludedFrameIndex = ExcludedIndex;
		Entry.FrameData = &ExcludedFrame.FrameData;
		Entry.ExtractionInfo = &ExcludedFrame.ExtractionInfo;
		Entry.Sprite = ExcludedFrame.KeyFrame.Sprite;
	}

	DisplayEntries.Sort([](const FDisplayedAlignmentFrameEntry& A, const FDisplayedAlignmentFrameEntry& B)
	{
		if (A.SourceFrameIndex != B.SourceFrameIndex)
		{
			return A.SourceFrameIndex < B.SourceFrameIndex;
		}
		if (A.bExcluded != B.bExcluded)
		{
			return !A.bExcluded;
		}
		if (!A.bExcluded && !B.bExcluded)
		{
			return A.ActiveFrameIndex < B.ActiveFrameIndex;
		}
		return A.ExcludedFrameIndex < B.ExcludedFrameIndex;
	});

	for (const FDisplayedAlignmentFrameEntry& Entry : DisplayEntries)
	{
		const bool bSelected = !Entry.bExcluded && (Entry.ActiveFrameIndex == SelectedFrameIndex);
		const bool bMultiSelected = !Entry.bExcluded && SelectedFrames.Contains(Entry.ActiveFrameIndex);
		const bool bHasCustomAlign = Entry.ExtractionInfo && Entry.ExtractionInfo->bHasCustomAlignment;
		const int32 DisplayNumber = Entry.SourceFrameIndex != INDEX_NONE ? Entry.SourceFrameIndex + 1 : 0;
		const FText FrameDisplayName = (Entry.FrameData && !Entry.FrameData->FrameName.IsEmpty())
			? FText::FromString(Entry.FrameData->FrameName)
			: FText::Format(LOCTEXT("AlignFrameFmt", "Frame {0}"), FText::AsNumber(DisplayNumber));
		const FText FrameToolTip = Entry.bExcluded
			? FText::Format(LOCTEXT("AlignFrameExcludedTooltipFmt", "{0}\nExcluded from live flipbook"), FrameDisplayName)
			: FrameDisplayName;
		const FText ToggleText = Entry.bExcluded ? LOCTEXT("FrameRestoreGlyph", "+") : LOCTEXT("FrameExcludeGlyph", "-");
		const FText ToggleToolTip = Entry.bExcluded
			? LOCTEXT("RestoreFrameTooltip", "Restore this frame")
			: LOCTEXT("ExcludeFrameTooltip", "Exclude this frame");
		const FLinearColor LabelColor = Entry.bExcluded
			? FLinearColor(0.55f, 0.55f, 0.55f, 1.0f)
			: FLinearColor::White;

		TSharedRef<SWidget> ThumbnailWidget = Entry.Sprite
			? StaticCastSharedRef<SWidget>(SNew(SSpriteThumbnail).Sprite(Entry.Sprite))
			: StaticCastSharedRef<SWidget>(SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::AsNumber(DisplayNumber))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
					.ColorAndOpacity(FSlateColor(LabelColor))
				]);

		TSharedRef<TSharedPtr<SBorder>> CardHoverTargetRef = MakeShared<TSharedPtr<SBorder>>();
		TSharedRef<SWidget> CardWidget = SNew(SBox)
			.WidthOverride(96.0f)
			.HeightOverride(80.0f)
			[
				SAssignNew(*CardHoverTargetRef, SBorder)
				.BorderImage(FAppStyle::GetBrush("NoBorder"))
				.Padding(0)
				.OnMouseButtonDown_Lambda([this, Entry](const FGeometry&, const FPointerEvent& MouseEvent) -> FReply
				{
					if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
					{
						if (!Entry.bExcluded && Entry.ActiveFrameIndex != INDEX_NONE)
						{
							OnFrameSelected(Entry.ActiveFrameIndex);
						}

						ShowSpriteContextMenu(Entry.Sprite, MouseEvent.GetScreenSpacePosition());
						return FReply::Handled();
					}

					return FReply::Unhandled();
				})
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush((bSelected || bMultiSelected) ? "Brushes.Primary" : "Brushes.Header"))
					.BorderBackgroundColor(bSelected ? ActiveFrameColor : (bMultiSelected ? SelectedFrameHighlightColor : FLinearColor::White))
					.Padding(FMargin(4, 4))
					.ToolTipText(FrameToolTip)
					[
						SNew(SOverlay)
						+ SOverlay::Slot()
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot()
							.AutoHeight()
							.HAlign(HAlign_Center)
							.Padding(0, 0, 0, 2)
							[
								SNew(SBox)
								.WidthOverride(44)
								.HeightOverride(44)
								[
									SNew(SOverlay)
									+ SOverlay::Slot()
									[
										ThumbnailWidget
									]
									+ SOverlay::Slot()
									[
										Entry.bExcluded
											? StaticCastSharedRef<SWidget>(SNew(SBorder)
												.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
												.Padding(0)
												.BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.42f)))
											: StaticCastSharedRef<SWidget>(SNullWidget::NullWidget)
									]
								]
							]
							+ SVerticalBox::Slot()
							.AutoHeight()
							.HAlign(HAlign_Center)
							[
								SNew(STextBlock)
								.Text(FText::Format(LOCTEXT("AlignFrameLabel", "F{0}"), FText::AsNumber(DisplayNumber)))
								.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
								.ColorAndOpacity(FSlateColor(LabelColor))
							]
							+ SVerticalBox::Slot()
							.AutoHeight()
							.HAlign(HAlign_Center)
							.Padding(0, 1, 0, 0)
							[
								SNew(STextBlock)
								.Text_Lambda([bHasCustomAlign, bExcluded = Entry.bExcluded]() -> FText {
									if (bExcluded) return FText::GetEmpty();
									return bHasCustomAlign ? LOCTEXT("AlignedMarkLong", "* custom") : LOCTEXT("AlignedMarkEmpty", "");
								})
								.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
								.ColorAndOpacity(Entry.bExcluded ? FLinearColor(0.75f, 0.45f, 0.45f) : FLinearColor::Yellow)
							]
						]
						+ SOverlay::Slot()
						.HAlign(HAlign_Left)
						.VAlign(VAlign_Top)
						.Padding(FMargin(1, 1, 0, 0))
						[
							SNew(SBox)
							.Visibility_Lambda([CardHoverTargetRef, bSelected, bExcluded = Entry.bExcluded]() -> EVisibility
							{
								const bool bHovered = CardHoverTargetRef->IsValid() && (*CardHoverTargetRef)->IsHovered();
								return (bExcluded || bSelected || bHovered) ? EVisibility::Visible : EVisibility::Collapsed;
							})
							[
								SNew(SButton)
								.ButtonStyle(FAppStyle::Get(), "NoBorder")
								.ContentPadding(FMargin(0))
								.ToolTipText(ToggleToolTip)
								.IsEnabled(!Entry.bExcluded ? (ActiveFrameCount > 1) : true)
								.OnClicked_Lambda([this, Entry]() -> FReply {
									if (Entry.bExcluded)
									{
										OnRestoreExcludedAlignmentFrame(Entry.ExcludedFrameIndex);
									}
									else if (Entry.ActiveFrameIndex != INDEX_NONE)
									{
										SelectedFrameIndex = Entry.ActiveFrameIndex;
										OnExcludeCurrentAlignmentFrame();
									}
									return FReply::Handled();
								})
								[
									SNew(SBorder)
									.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
									.BorderBackgroundColor(Entry.bExcluded
										? FLinearColor(0.2f, 0.45f, 0.2f, 1.0f)
										: FLinearColor(0.45f, 0.2f, 0.2f, 1.0f))
									.Padding(FMargin(1))
									[
										SNew(SBox)
										.WidthOverride(12.0f)
										.HeightOverride(12.0f)
										.HAlign(HAlign_Center)
										.VAlign(VAlign_Center)
										[
											SNew(STextBlock)
											.Text(ToggleText)
											.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
											.ColorAndOpacity(FSlateColor(FLinearColor::White))
											.Justification(ETextJustify::Center)
										]
									]
								]
							]
						]
					]
				]
			];

		if (!Entry.bExcluded)
		{
			TSharedPtr<SFrameDragDropWrapper> Wrapper;
			AlignmentFrameListBox->AddSlot()
			.AutoWidth()
			.Padding(0, 0, 4, 0)
			[
				SAssignNew(Wrapper, SFrameDragDropWrapper)
				[
					CardWidget
				]
			];

			Wrapper->FrameIndex = Entry.ActiveFrameIndex;
			Wrapper->OnClickedFunc = [this, ActiveFrameIndex = Entry.ActiveFrameIndex](const FPointerEvent& MouseEvent)
			{
				FrameSelectionUtils::HandleFrameClick(SelectedFrames, FrameSelectionAnchorIndex, ActiveFrameIndex, MouseEvent, GetCurrentFrameCount());
				OnFrameSelected(ActiveFrameIndex);
				RefreshAlignmentFrameList();
			};
			Wrapper->OnFrameDroppedFunc = [this](int32 From, int32 To)
			{
				ReorderFrame(From, To);
			};
		}
		else
		{
			AlignmentFrameListBox->AddSlot()
			.AutoWidth()
			.Padding(0, 0, 4, 0)
			[
				CardWidget
			];
		}
	}

}

void SCharacterProfileAssetEditor::NudgeOffset(int32 DeltaX, int32 DeltaY)
{
	// Pause queue playback during offset editing
	if (bIsPlaying && PlaybackQueue.Num() > 0) StopPlayback();

	FFlipbookHitboxData* Anim = GetCurrentFlipbookDataMutable();
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

void SCharacterProfileAssetEditor::OnOffsetXChanged(int32 NewValue)
{
	if (bIsPlaying && PlaybackQueue.Num() > 0) StopPlayback();

	FFlipbookHitboxData* Anim = GetCurrentFlipbookDataMutable();
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

void SCharacterProfileAssetEditor::OnOffsetYChanged(int32 NewValue)
{
	if (bIsPlaying && PlaybackQueue.Num() > 0) StopPlayback();

	FFlipbookHitboxData* Anim = GetCurrentFlipbookDataMutable();
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

void SCharacterProfileAssetEditor::OnCopyOffset()
{
	const FFlipbookHitboxData* Anim = GetCurrentFlipbookData();
	if (!Anim || !Anim->FrameExtractionInfo.IsValidIndex(SelectedFrameIndex))
	{
		CopiedOffset = FIntPoint::ZeroValue;
		bHasCopiedOffset = true;
		return;
	}

	CopiedOffset = Anim->FrameExtractionInfo[SelectedFrameIndex].SpriteOffset;
	bHasCopiedOffset = true;
}

void SCharacterProfileAssetEditor::OnPasteOffset()
{
	if (!bHasCopiedOffset) return;

	FFlipbookHitboxData* Anim = GetCurrentFlipbookDataMutable();
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

void SCharacterProfileAssetEditor::OnApplyOffsetToAll()
{
	FFlipbookHitboxData* Anim = GetCurrentFlipbookDataMutable();
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

void SCharacterProfileAssetEditor::OnApplyOffsetToRemaining()
{
	FFlipbookHitboxData* Anim = GetCurrentFlipbookDataMutable();
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

void SCharacterProfileAssetEditor::OnResetOffset()
{
	FFlipbookHitboxData* Anim = GetCurrentFlipbookDataMutable();
	if (!Anim || !Anim->FrameExtractionInfo.IsValidIndex(SelectedFrameIndex)) return;

	BeginTransaction(LOCTEXT("ResetOffset", "Reset Sprite Offset"));
	Asset->Modify();

	Anim->FrameExtractionInfo[SelectedFrameIndex].SpriteOffset = FIntPoint::ZeroValue;
	Anim->FrameExtractionInfo[SelectedFrameIndex].bHasCustomAlignment = false;

	EndTransaction();
	RefreshAlignmentFrameList();
}

void SCharacterProfileAssetEditor::OnAlignmentOffsetChanged(int32 DeltaX, int32 DeltaY)
{
	NudgeOffset(DeltaX, DeltaY);
}

void SCharacterProfileAssetEditor::StartPlayback()
{
	if (bIsPlaying) return;

	ClearFrameSelection();
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
		if (Asset.IsValid() && Asset->Flipbooks.IsValidIndex(AnimIdx))
		{
			UPaperFlipbook* FB = Asset->Flipbooks[AnimIdx].Flipbook.LoadSynchronous();
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
		FTickerDelegate::CreateSP(this, &SCharacterProfileAssetEditor::OnPlaybackTick),
		1.0f / 60.0f
	);

	RefreshPlaybackQueueList();
}

void SCharacterProfileAssetEditor::StopPlayback()
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

void SCharacterProfileAssetEditor::TogglePlayback()
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

int32 SCharacterProfileAssetEditor::FrameIndexFromPlaybackPosition(
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

bool SCharacterProfileAssetEditor::OnPlaybackTick(float DeltaTime)
{
	if (!Asset.IsValid()) return true;

	if (PlaybackQueue.Num() > 0)
	{
		return OnQueuePlaybackTick(DeltaTime);
	}

	// Single flipbook playback (time-based)
	const FFlipbookHitboxData* Anim = GetCurrentFlipbookData();
	if (!Anim || Anim->Flipbook.IsNull()) return true;

	UPaperFlipbook* FB = Anim->Flipbook.LoadSynchronous();
	if (!FB) return true;

	FFlipbookTimingData Timing = FFlipbookTimingData::ReadFromFlipbook(FB);
	if (Timing.TotalDurationSeconds <= 0.0f) return true;

	if (bPingPongPlayback)
	{
		if (bPlaybackReversed)
		{
			PlaybackPosition -= DeltaTime;
			if (PlaybackPosition < 0.0f)
			{
				PlaybackPosition = FMath::Abs(PlaybackPosition);
				bPlaybackReversed = false;
			}
		}
		else
		{
			PlaybackPosition += DeltaTime;
			if (PlaybackPosition >= Timing.TotalDurationSeconds)
			{
				PlaybackPosition = Timing.TotalDurationSeconds - (PlaybackPosition - Timing.TotalDurationSeconds);
				PlaybackPosition = FMath::Max(0.0f, PlaybackPosition);
				bPlaybackReversed = true;
			}
		}
	}
	else
	{
		PlaybackPosition += DeltaTime;
		if (PlaybackPosition >= Timing.TotalDurationSeconds)
		{
			PlaybackPosition = FMath::Fmod(PlaybackPosition, Timing.TotalDurationSeconds);
		}
	}

	int32 NewFrameIndex = FrameIndexFromPlaybackPosition(Timing, PlaybackPosition);

	if (NewFrameIndex != SelectedFrameIndex)
	{
		SelectedFrameIndex = NewFrameIndex;
		RefreshAlignmentFrameList();
	}

	return true;
}

bool SCharacterProfileAssetEditor::OnQueuePlaybackTick(float DeltaTime)
{
	// Validate current queue entry — skip invalid entries
	while (PlaybackQueueIndex < PlaybackQueue.Num())
	{
		int32 AnimIdx = PlaybackQueue[PlaybackQueueIndex];
		if (Asset->Flipbooks.IsValidIndex(AnimIdx)
			&& !Asset->Flipbooks[AnimIdx].Flipbook.IsNull())
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
			if (Asset->Flipbooks.IsValidIndex(AnimIdx)
				&& !Asset->Flipbooks[AnimIdx].Flipbook.IsNull())
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
	FFlipbookHitboxData& Anim = Asset->Flipbooks[AnimIdx];

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

	if (bPingPongPlayback)
	{
		if (bPlaybackReversed)
		{
			PlaybackPosition -= DeltaTime;
			if (PlaybackPosition < 0.0f)
			{
				// Completed a full forward+backward cycle — advance to next queue entry
				float Overflow = FMath::Abs(PlaybackPosition);
				PlaybackQueueIndex++;
				PlaybackPosition = Overflow;
				bPlaybackReversed = false;
				CachedPlaybackTiming = FFlipbookTimingData();

				if (PlaybackQueueIndex < PlaybackQueue.Num())
				{
					SyncSelectionToQueueEntry(PlaybackQueueIndex);
				}
				RefreshPlaybackQueueList();
				return true;
			}
		}
		else
		{
			PlaybackPosition += DeltaTime;
			if (PlaybackPosition >= CachedPlaybackTiming.TotalDurationSeconds)
			{
				PlaybackPosition = CachedPlaybackTiming.TotalDurationSeconds - (PlaybackPosition - CachedPlaybackTiming.TotalDurationSeconds);
				PlaybackPosition = FMath::Max(0.0f, PlaybackPosition);
				bPlaybackReversed = true;
			}
		}
	}
	else
	{
		PlaybackPosition += DeltaTime;

		// Check if we've exceeded this flipbook's duration
		if (PlaybackPosition >= CachedPlaybackTiming.TotalDurationSeconds)
		{
			float Overflow = PlaybackPosition - CachedPlaybackTiming.TotalDurationSeconds;
			PlaybackQueueIndex++;
			PlaybackPosition = Overflow;
			CachedPlaybackTiming = FFlipbookTimingData();

			if (PlaybackQueueIndex < PlaybackQueue.Num())
			{
				SyncSelectionToQueueEntry(PlaybackQueueIndex);
			}
			RefreshPlaybackQueueList();
			return true;
		}
	}

	// Sync selection if flipbook changed
	if (AnimIdx != SelectedFlipbookIndex)
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

void SCharacterProfileAssetEditor::SyncSelectionToQueueEntry(int32 QueueIndex)
{
	if (!PlaybackQueue.IsValidIndex(QueueIndex)) return;

	int32 AnimIdx = PlaybackQueue[QueueIndex];
	if (AnimIdx != SelectedFlipbookIndex)
	{
		SelectedFlipbookIndex = AnimIdx;
		SelectedFrameIndex = 0;
		RefreshAlignmentFlipbookList();
		RefreshAlignmentFrameList();
		RefreshPlaybackQueueList();
	}
}

void SCharacterProfileAssetEditor::AddToPlaybackQueue(int32 FlipbookIndex)
{
	if (Asset.IsValid() && Asset->Flipbooks.IsValidIndex(FlipbookIndex))
	{
		PlaybackQueue.Add(FlipbookIndex);
		// Select the added flipbook so the canvas and other tabs reflect it
		OnFlipbookSelected(FlipbookIndex);
	}
}

void SCharacterProfileAssetEditor::RemoveFromPlaybackQueue(int32 QueueIndex)
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

void SCharacterProfileAssetEditor::ClearPlaybackQueue()
{
	if (bIsPlaying) StopPlayback();
	PlaybackQueue.Empty();
	PlaybackQueueIndex = 0;
	PlaybackPosition = 0.0f;
	CachedPlaybackTiming = FFlipbookTimingData();
	RefreshPlaybackQueueList();
}

void SCharacterProfileAssetEditor::ReorderQueueEntry(int32 FromIndex, int32 ToIndex)
{
	// ToIndex = insert-before position in the *original* array. Num() means append to end.
	if (!PlaybackQueue.IsValidIndex(FromIndex) || ToIndex < 0 || ToIndex > PlaybackQueue.Num()) return;
	if (FromIndex == ToIndex) return;

	int32 MovedAnim = PlaybackQueue[FromIndex];
	PlaybackQueue.RemoveAt(FromIndex);

	// After removal, adjust target index for the shift
	int32 InsertAt = (ToIndex > FromIndex) ? ToIndex - 1 : ToIndex;
	InsertAt = FMath::Clamp(InsertAt, 0, PlaybackQueue.Num());
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

int32 SCharacterProfileAssetEditor::GetAdjacentFlipbookIndex(int32 Direction) const
{
	if (!Asset.IsValid() || Asset->Flipbooks.Num() <= 1) return INDEX_NONE;

	// If queue is active, use queue order
	if (PlaybackQueue.Num() > 0)
	{
		int32 CurrentQueuePos = INDEX_NONE;
		for (int32 i = 0; i < PlaybackQueue.Num(); i++)
		{
			if (PlaybackQueue[i] == SelectedFlipbookIndex)
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

	// No queue — navigate sorted flipbook list with wrapping
	TArray<int32> SortedIndices = GetSortedFlipbookIndices();
	if (SortedIndices.Num() <= 1) return INDEX_NONE;

	int32 CurrentPos = SortedIndices.IndexOfByKey(SelectedFlipbookIndex);
	if (CurrentPos == INDEX_NONE) return INDEX_NONE;

	int32 TargetPos = CurrentPos + Direction;
	if (!SortedIndices.IsValidIndex(TargetPos)) return INDEX_NONE;
	return SortedIndices[TargetPos];
}

void SCharacterProfileAssetEditor::OnEditAlignmentClicked(int32 FlipbookIndex)
{
	SelectedFlipbookIndex = FlipbookIndex;
	SelectedFrameIndex = 0;
	SwitchToTab(2);
}

bool SCharacterProfileAssetEditor::CanExcludeCurrentAlignmentFrame() const
{
	if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex))
	{
		return false;
	}

	UPaperFlipbook* Flipbook = Asset->Flipbooks[SelectedFlipbookIndex].Flipbook.LoadSynchronous();
	return Flipbook && Flipbook->GetNumKeyFrames() > 1 &&
		SelectedFrameIndex >= 0 && SelectedFrameIndex < Flipbook->GetNumKeyFrames();
}

void SCharacterProfileAssetEditor::OnExcludeCurrentAlignmentFrame()
{
	if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex))
	{
		return;
	}

	UPaperFlipbook* Flipbook = Asset->Flipbooks[SelectedFlipbookIndex].Flipbook.LoadSynchronous();
	if (!Flipbook || !CanExcludeCurrentAlignmentFrame())
	{
		return;
	}

	if (bIsPlaying)
	{
		StopPlayback();
	}

	BeginTransaction(LOCTEXT("ExcludeAlignmentFrameTxn", "Exclude Sprite Frame"));
	Flipbook->SetFlags(RF_Transactional);
	Flipbook->Modify();
	const bool bExcluded = Asset->ExcludeFlipbookFrame(SelectedFlipbookIndex, SelectedFrameIndex);
	EndTransaction();

	if (!bExcluded)
	{
		return;
	}

	const int32 ExcludedIndex = SelectedFrameIndex;
	const int32 NewFrameCount = Flipbook->GetNumKeyFrames();
	SelectedFrameIndex = NewFrameCount > 0
		? FMath::Clamp(SelectedFrameIndex, 0, NewFrameCount - 1)
		: 0;

	if (ReferenceFlipbookIndex == SelectedFlipbookIndex)
	{
		if (ExcludedIndex < ReferenceFrameIndex)
		{
			// Shift reference down to preserve identity
			ReferenceFrameIndex--;
		}
		else if (ExcludedIndex == ReferenceFrameIndex)
		{
			// Reference itself was excluded — clamp to valid range
			ReferenceFrameIndex = NewFrameCount > 0
				? FMath::Clamp(ReferenceFrameIndex, 0, NewFrameCount - 1)
				: 0;
		}
		// ExcludedIndex > ReferenceFrameIndex: no change needed
	}

	RefreshAfterFrameExclusion();
}

void SCharacterProfileAssetEditor::OnRestoreExcludedAlignmentFrame(int32 ExcludedFrameIndex)
{
	if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex))
	{
		return;
	}

	FFlipbookHitboxData& Anim = Asset->Flipbooks[SelectedFlipbookIndex];
	if (!Anim.ExcludedFrames.IsValidIndex(ExcludedFrameIndex))
	{
		return;
	}

	UPaperFlipbook* Flipbook = Anim.Flipbook.LoadSynchronous();
	if (!Flipbook)
	{
		return;
	}

	const int32 SourceFrameIndex = Anim.ExcludedFrames[ExcludedFrameIndex].ExtractionInfo.SourceFrameIndex;

	BeginTransaction(LOCTEXT("RestoreExcludedAlignmentFrameTxn", "Restore Excluded Sprite Frame"));
	Flipbook->SetFlags(RF_Transactional);
	Flipbook->Modify();
	const bool bRestored = Asset->RestoreExcludedFlipbookFrame(SelectedFlipbookIndex, ExcludedFrameIndex);
	EndTransaction();

	if (!bRestored || !Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex))
	{
		return;
	}

	const FFlipbookHitboxData& UpdatedAnim = Asset->Flipbooks[SelectedFlipbookIndex];
	const int32 RestoredIndex = FindAlignmentFrameIndexBySourceIndex(UpdatedAnim, SourceFrameIndex);
	if (RestoredIndex != INDEX_NONE)
	{
		SelectedFrameIndex = RestoredIndex;
	}
	else
	{
		SelectedFrameIndex = FMath::Clamp(SelectedFrameIndex, 0, FMath::Max(0, Flipbook->GetNumKeyFrames() - 1));
	}

	RefreshAfterFrameExclusion(/*bDismissMenus=*/ true);
}

void SCharacterProfileAssetEditor::OnRestoreAllExcludedAlignmentFrames()
{
	if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex))
	{
		return;
	}

	FFlipbookHitboxData& Anim = Asset->Flipbooks[SelectedFlipbookIndex];
	if (Anim.ExcludedFrames.Num() <= 0)
	{
		return;
	}

	UPaperFlipbook* Flipbook = Anim.Flipbook.LoadSynchronous();
	if (!Flipbook)
	{
		return;
	}

	BeginTransaction(LOCTEXT("RestoreAllExcludedAlignmentFramesTxn", "Restore All Excluded Sprite Frames"));
	Flipbook->SetFlags(RF_Transactional);
	Flipbook->Modify();
	const int32 RestoredCount = Asset->RestoreAllExcludedFlipbookFrames(SelectedFlipbookIndex);
	EndTransaction();

	if (RestoredCount <= 0)
	{
		return;
	}

	SelectedFrameIndex = FMath::Clamp(SelectedFrameIndex, 0, FMath::Max(0, Flipbook->GetNumKeyFrames() - 1));

	RefreshAfterFrameExclusion(/*bDismissMenus=*/ true);
}

void SCharacterProfileAssetEditor::RefreshAfterFrameExclusion(bool bDismissMenus)
{
	ClearFrameSelection();
	RefreshCurrentFrameFlipState();
	RefreshAlignmentFrameList();
	RefreshFrameList();
	RefreshFlipbookList();
	RefreshOverviewFlipbookList();
	RefreshAlignmentFlipbookList();
	RefreshPlaybackQueueList();
	if (bDismissMenus)
	{
		FSlateApplication::Get().DismissAllMenus();
	}
	if (AlignmentCanvas.IsValid())
	{
		AlignmentCanvas->InvalidateCachedDims();
	}
}

TSharedRef<SWidget> SCharacterProfileAssetEditor::BuildAlignmentRestoreExcludedMenu()
{
	if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex))
	{
		return SNew(STextBlock).Text(LOCTEXT("NoAlignmentFlipbookSelected", "No flipbook selected"));
	}

	const FFlipbookHitboxData& Anim = Asset->Flipbooks[SelectedFlipbookIndex];
	if (Anim.ExcludedFrames.Num() <= 0)
	{
		return SNew(STextBlock).Text(LOCTEXT("NoAlignmentExcludedFrames", "No excluded frames"));
	}

	TArray<int32> SortedIndices;
	SortedIndices.Reserve(Anim.ExcludedFrames.Num());
	for (int32 Index = 0; Index < Anim.ExcludedFrames.Num(); ++Index)
	{
		SortedIndices.Add(Index);
	}
	SortedIndices.Sort([&Anim](int32 A, int32 B)
	{
		return Anim.ExcludedFrames[A].ExtractionInfo.SourceFrameIndex <
			Anim.ExcludedFrames[B].ExtractionInfo.SourceFrameIndex;
	});

	TSharedRef<SVerticalBox> MenuBox = SNew(SVerticalBox);

	MenuBox->AddSlot()
	.AutoHeight()
	.Padding(2, 0, 2, 4)
	[
		SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
		.OnClicked_Lambda([this]() {
			OnRestoreAllExcludedAlignmentFrames();
			return FReply::Handled();
		})
		[
			SNew(STextBlock)
			.Text(LOCTEXT("RestoreAllExcludedAlignment", "Restore All"))
		]
	];

	for (int32 ExcludedIndex : SortedIndices)
	{
		const FExcludedFlipbookFrameData& ExcludedFrame = Anim.ExcludedFrames[ExcludedIndex];
		const int32 SourceFrameNumber = FMath::Max(0, ExcludedFrame.ExtractionInfo.SourceFrameIndex) + 1;
		const FText FrameNameText = ExcludedFrame.FrameData.FrameName.IsEmpty()
			? FText::Format(LOCTEXT("ExcludedAlignmentFrameDefaultName", "Frame {0}"), FText::AsNumber(SourceFrameNumber))
			: FText::FromString(ExcludedFrame.FrameData.FrameName);

		MenuBox->AddSlot()
		.AutoHeight()
		.Padding(2, 0, 2, 2)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.OnClicked_Lambda([this, ExcludedIndex]() {
				OnRestoreExcludedAlignmentFrame(ExcludedIndex);
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Text(FText::Format(LOCTEXT("RestoreOneExcludedAlignmentFmt", "Restore #{0}: {1}"),
					FText::AsNumber(SourceFrameNumber),
					FrameNameText))
			]
		];
	}

	return SNew(SBox)
		.MinDesiredWidth(260.0f)
		.MaxDesiredHeight(280.0f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				MenuBox
			]
		];
}

void SCharacterProfileAssetEditor::RefreshCurrentFrameFlipState()
{
	const FFlipbookHitboxData* Anim = GetCurrentFlipbookData();
	if (!Anim || !Anim->FrameExtractionInfo.IsValidIndex(SelectedFrameIndex))
	{
		bSpriteFlipX = false;
		bSpriteFlipY = false;
		return;
	}

	bSpriteFlipX = Anim->FrameExtractionInfo[SelectedFrameIndex].bFlipX;
	bSpriteFlipY = Anim->FrameExtractionInfo[SelectedFrameIndex].bFlipY;
}

void SCharacterProfileAssetEditor::OnApplyFlipToCurrentFrame()
{
	if (!Asset.IsValid()) return;
	FFlipbookHitboxData* Anim = GetCurrentFlipbookDataMutable();
	if (!Anim || !Anim->Frames.IsValidIndex(SelectedFrameIndex)) return;

	BeginTransaction(LOCTEXT("ApplyFlipCurrentFrameTxn", "Apply Sprite Flip to Current Frame"));
	Asset->SetSpriteFlipInRange(Anim->FlipbookName, SelectedFrameIndex, SelectedFrameIndex, bSpriteFlipX, bSpriteFlipY);
	EndTransaction();
	RefreshCurrentFrameFlipState();
}

void SCharacterProfileAssetEditor::OnApplyFlipToCurrentFlipbook()
{
	if (!Asset.IsValid()) return;
	FFlipbookHitboxData* Anim = GetCurrentFlipbookDataMutable();
	if (!Anim || Anim->Frames.Num() == 0) return;

	BeginTransaction(LOCTEXT("ApplyFlipCurrentFlipbookTxn", "Apply Sprite Flip to Flipbook"));
	Asset->SetSpriteFlipForFlipbook(Anim->FlipbookName, bSpriteFlipX, bSpriteFlipY);
	EndTransaction();
	RefreshCurrentFrameFlipState();
}

void SCharacterProfileAssetEditor::OnApplyFlipToAllFlipbooks()
{
	if (!Asset.IsValid()) return;

	BeginTransaction(LOCTEXT("ApplyFlipAllFlipbooksTxn", "Apply Sprite Flip to All Flipbooks"));
	Asset->SetSpriteFlipForAllFlipbooks(bSpriteFlipX, bSpriteFlipY);
	EndTransaction();
	RefreshCurrentFrameFlipState();
}

void SCharacterProfileAssetEditor::ReorderFrame(int32 FromIndex, int32 ToIndex)
{
	if (!Asset.IsValid()) return;
	FFlipbookHitboxData* Anim = GetCurrentFlipbookDataMutable();
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
