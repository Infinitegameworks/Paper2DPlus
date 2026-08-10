// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "LayerOverviewPanel.h"

#include "AnimationTimeline.h"
#include "CharacterProfileEditorModel.h"
#include "Editor.h"
#include "EditorCanvasUtils.h"
#include "LayerCompositeThumbnail.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "PaperFlipbook.h"
#include "SlateShortcutUtils.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "LayerOverviewPanel"

void SLayerOverviewPanel::Construct(const FArguments& InArgs)
{
	Model = InArgs._Model;
	LayerAsset = InArgs._LayerAsset;
	bLayerFirstPresentation = InArgs._LayerFirstPresentation;
	if (GEditor)
	{
		GEditor->RegisterForUndo(this);
	}
	if (Model.IsValid())
	{
		FlipbookSelectionHandle = Model->OnFlipbookSelectionChanged.AddLambda([this](int32)
		{
			StopPlayback();
			RefreshPreview();
			RefreshFrameStrip();
		});
		FrameSelectionHandle = Model->OnFrameSelectionChanged.AddLambda([this]()
		{
			RefreshPreview();
			ScrollSelectedFrameIntoView();
		});
		QueuePlaybackStateHandle = Model->OnQueuePlaybackStateChanged.AddLambda([this](bool bQueuePlaying)
		{
			if (bQueuePlaying)
			{
				StopPlayback();
			}
		});
	}

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(8.0f, 8.0f, 8.0f, 0.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text_Lambda([this]()
				{
					const int32 FrameCount = GetCurrentFrameCount();
					const int32 FrameNumber = FrameCount > 0 && Model.IsValid()
						? FMath::Clamp(Model->GetSelectedFrameIndex(), 0, FrameCount - 1) + 1
						: 0;
					return FText::Format(
						bLayerFirstPresentation
							? LOCTEXT("LayerPreviewStatus", "Visible Layers  |  {0}  |  Frame {1} of {2}")
							: LOCTEXT("CompositePreviewStatus", "Composite  |  {0}  |  Frame {1} of {2}"),
						FText::FromString(GetCurrentAnimationName()),
						FText::AsNumber(FrameNumber),
						FText::AsNumber(FrameCount));
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("PlaybackHint", "Space: Play/Pause"))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ToolTipText(LOCTEXT("PlaybackTooltip", "Play or pause the current animation (Space)."))
				.OnClicked_Lambda([this]()
				{
					if (Model.IsValid() && Model->IsQueueActive())
					{
						StopPlayback();
						Model->SetQueuePlaying(!Model->IsQueuePlaying());
					}
					else
					{
						TogglePlayback();
					}
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text_Lambda([this]()
					{
						const bool bAnyPlayback = bIsPlaying || (Model.IsValid() && Model->IsQueuePlaying());
						return bAnyPlayback ? LOCTEXT("Pause", "Pause") : LOCTEXT("Play", "Play");
					})
				]
			]
		]
		+ SVerticalBox::Slot().FillHeight(1.0f).Padding(8.0f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
			.Padding(1.0f)
			[
				SNew(SBox)
				.MinDesiredWidth(320.0f)
				.MinDesiredHeight(320.0f)
				[
					SNew(SOverlay)
					+ SOverlay::Slot()
					[
						SAssignNew(PreviewCanvas, SLayerCompositeThumbnail)
						.LayerAsset(LayerAsset)
						.Model(Model)
						.FrameIndex(Model.IsValid() ? Model->GetSelectedFrameIndex() : 0)
						.DrawCheckerboard(true)
					]
					+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("NoVisibleLayerArt", "No visible Layer art for this frame."))
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
						.Visibility_Lambda([this]()
						{
							return PreviewCanvas.IsValid() && !PreviewCanvas->HasPaintItems()
								? EVisibility::HitTestInvisible
								: EVisibility::Collapsed;
						})
					]
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(8.0f, 0.0f, 8.0f, 8.0f)
		[
			BuildFrameStrip()
		]
		]
		+ SOverlay::Slot()
		[
			SAssignNew(KeyboardFocusBorder, SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("FocusRectangle"))
			.BorderBackgroundColor(this, &SLayerOverviewPanel::GetKeyboardFocusIndicatorColor)
			.Padding(0.0f)
			.Visibility(EVisibility::HitTestInvisible)
		]
	];
	RefreshPreview();
	RefreshFrameStrip();
}

SLayerOverviewPanel::~SLayerOverviewPanel()
{
	StopPlayback();
	if (GEditor)
	{
		GEditor->UnregisterForUndo(this);
	}
	if (Model.IsValid())
	{
		Model->OnFlipbookSelectionChanged.Remove(FlipbookSelectionHandle);
		Model->OnFrameSelectionChanged.Remove(FrameSelectionHandle);
		Model->OnQueuePlaybackStateChanged.Remove(QueuePlaybackStateHandle);
	}
}

void SLayerOverviewPanel::PostUndo(bool bSuccess)
{
	if (bSuccess)
	{
		if (PreviewCanvas.IsValid())
		{
			PreviewCanvas->RefreshFromModel();
		}
		RefreshFrameStrip();
	}
}

void SLayerOverviewPanel::PostRedo(bool bSuccess)
{
	if (bSuccess)
	{
		if (PreviewCanvas.IsValid())
		{
			PreviewCanvas->RefreshFromModel();
		}
		RefreshFrameStrip();
	}
}

void SLayerOverviewPanel::HandleAuthoringModeDeactivated()
{
	StopPlayback();
}

void SLayerOverviewPanel::OnFocusChanging(
	const FWeakWidgetPath& PreviousFocusPath,
	const FWidgetPath& NewWidgetPath,
	const FFocusEvent& InFocusEvent)
{
	SCompoundWidget::OnFocusChanging(PreviousFocusPath, NewWidgetPath, InFocusEvent);
	if (KeyboardFocusBorder.IsValid())
	{
		KeyboardFocusBorder->Invalidate(EInvalidateWidgetReason::Paint);
	}
}

FReply SLayerOverviewPanel::OnMouseButtonDown(const FGeometry&, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		return FReply::Handled().SetUserFocus(SharedThis(this), EFocusCause::Mouse);
	}
	return FReply::Unhandled();
}

FReply SLayerOverviewPanel::OnKeyDown(const FGeometry&, const FKeyEvent& Event)
{
	if (!Model.IsValid())
	{
		return FReply::Unhandled();
	}
	if (Paper2DPlusEditor::SlateShortcutUtils::ShouldIgnoreShortcutForFocusedWidget()
		|| Event.IsControlDown()
		|| Event.IsAltDown())
	{
		return FReply::Unhandled();
	}

	const FKey Key = Event.GetKey();
	if (Key == EKeys::SpaceBar)
	{
		if (Model->IsQueueActive())
		{
			StopPlayback();
			Model->SetQueuePlaying(!Model->IsQueuePlaying());
		}
		else
		{
			TogglePlayback();
		}
		return FReply::Handled();
	}

	const int32 FrameCount = GetCurrentFrameCount();
	const int32 SelectedFrame = Model->GetSelectedFrameIndex();
	if (Key == EKeys::Left || Key == EKeys::Comma)
	{
		SeekFrame(SelectedFrame > 0 ? SelectedFrame - 1 : FrameCount - 1);
		return FReply::Handled();
	}
	if (Key == EKeys::Right || Key == EKeys::Period)
	{
		SeekFrame(SelectedFrame < FrameCount - 1 ? SelectedFrame + 1 : 0);
		return FReply::Handled();
	}
	if (Key == EKeys::Home)
	{
		SeekFrame(0);
		return FReply::Handled();
	}
	if (Key == EKeys::End)
	{
		SeekFrame(FrameCount - 1);
		return FReply::Handled();
	}
	if (Key == EKeys::Up || Key == EKeys::Down)
	{
		StopPlayback();
		const int32 Adjacent = Model->GetVisualAdjacentFlipbookIndex(Key == EKeys::Up ? -1 : 1);
		if (Adjacent != INDEX_NONE && Adjacent != Model->GetSelectedFlipbookIndex())
		{
			Model->SetSelectedFlipbook(Adjacent);
		}
		return FReply::Handled();
	}
	if (Key == EKeys::Escape && bIsPlaying)
	{
		StopPlayback();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

void SLayerOverviewPanel::RefreshPreview()
{
	if (PreviewCanvas.IsValid())
	{
		PreviewCanvas->SetFrameIndex(Model.IsValid() ? Model->GetSelectedFrameIndex() : 0);
	}
}

TSharedRef<SWidget> SLayerOverviewPanel::BuildFrameStrip()
{
	return SAssignNew(FrameStripScrollBox, SScrollBox)
		.Orientation(Orient_Horizontal)
		+ SScrollBox::Slot()
		[
			SAssignNew(FrameStripBox, SHorizontalBox)
		];
}

void SLayerOverviewPanel::RefreshFrameStrip()
{
	if (!FrameStripBox.IsValid())
	{
		return;
	}
	FrameStripBox->ClearChildren();
	FrameCellWidgets.Reset();
	const int32 FrameCount = GetCurrentFrameCount();
	for (int32 FrameIndex = 0; FrameIndex < FrameCount; ++FrameIndex)
	{
		FFrameStripCellArgs CellArgs;
		CellArgs.FrameIndex = FrameIndex;
		CellArgs.SpriteContentOverride = SNew(SLayerCompositeThumbnail)
			.LayerAsset(LayerAsset)
			.Model(Model)
			.FrameIndex(FrameIndex);
		CellArgs.IsSelected = [WeakModel = TWeakPtr<FCharacterProfileEditorModel>(Model), FrameIndex]()
		{
			const TSharedPtr<FCharacterProfileEditorModel> Pinned = WeakModel.Pin();
			return Pinned.IsValid() && Pinned->GetSelectedFrameIndex() == FrameIndex;
		};
		CellArgs.OnMouseButtonDown = [this, FrameIndex](const FPointerEvent& MouseEvent)
		{
			if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
			{
				SeekFrame(FrameIndex);
				return FReply::Handled().SetUserFocus(SharedThis(this), EFocusCause::Mouse);
			}
			return FReply::Unhandled();
		};
		TSharedRef<SWidget> FrameCell = FFrameStripCellUtils::Build(CellArgs);
		FrameCellWidgets.Add(FrameCell);
		FrameStripBox->AddSlot().AutoWidth().Padding(2.0f)
		[
			FrameCell
		];
	}
}

void SLayerOverviewPanel::ScrollSelectedFrameIntoView()
{
	const int32 SelectedFrame = Model.IsValid() ? Model->GetSelectedFrameIndex() : INDEX_NONE;
	if (FrameStripScrollBox.IsValid() && FrameCellWidgets.IsValidIndex(SelectedFrame))
	{
		FrameStripScrollBox->ScrollDescendantIntoView(FrameCellWidgets[SelectedFrame].ToSharedRef(), true);
	}
}

FString SLayerOverviewPanel::GetCurrentAnimationName() const
{
	UPaper2DPlusCharacterProfileAsset* Profile = Model.IsValid() ? Model->GetAsset() : nullptr;
	const int32 FlipbookIndex = Model.IsValid() ? Model->GetSelectedFlipbookIndex() : INDEX_NONE;
	return Profile && Profile->Flipbooks.IsValidIndex(FlipbookIndex)
		? Profile->Flipbooks[FlipbookIndex].Identity.FlipbookName
		: FString(TEXT("No animation"));
}

UPaperFlipbook* SLayerOverviewPanel::GetCurrentFlipbook() const
{
	UPaper2DPlusCharacterProfileAsset* Profile = Model.IsValid() ? Model->GetAsset() : nullptr;
	const int32 FlipbookIndex = Model.IsValid() ? Model->GetSelectedFlipbookIndex() : INDEX_NONE;
	return Profile && Profile->Flipbooks.IsValidIndex(FlipbookIndex)
		? Profile->Flipbooks[FlipbookIndex].Identity.Flipbook.LoadSynchronous()
		: nullptr;
}

int32 SLayerOverviewPanel::GetCurrentFrameCount() const
{
	const UPaperFlipbook* Flipbook = GetCurrentFlipbook();
	return Flipbook ? Flipbook->GetNumKeyFrames() : 0;
}

void SLayerOverviewPanel::SeekFrame(int32 FrameIndex, bool bStopPlayback)
{
	if (!Model.IsValid())
	{
		return;
	}
	if (bStopPlayback)
	{
		StopPlayback();
	}
	const int32 FrameCount = GetCurrentFrameCount();
	if (FrameCount <= 0)
	{
		return;
	}
	Model->ClearFrameSelection();
	Model->SetSelectedFrame(FMath::Clamp(FrameIndex, 0, FrameCount - 1));
}

void SLayerOverviewPanel::StartPlayback()
{
	if (bIsPlaying || !Model.IsValid() || Model->IsQueuePlaying())
	{
		return;
	}
	UPaperFlipbook* Flipbook = GetCurrentFlipbook();
	if (!Flipbook || Flipbook->GetNumKeyFrames() <= 1)
	{
		return;
	}

	const FFlipbookTimingData Timing = FFlipbookTimingData::ReadFromFlipbook(Flipbook);
	if (Timing.TotalDurationSeconds <= 0.0f || Timing.FrameDurations.IsEmpty())
	{
		return;
	}
	Model->ClearFrameSelection();
	const int32 StartFrame = FMath::Clamp(Model->GetSelectedFrameIndex(), 0, Flipbook->GetNumKeyFrames() - 1);
	PlaybackPosition = Timing.GetFrameStartTime(StartFrame);
	bIsPlaying = true;
	PlaybackTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateSP(this, &SLayerOverviewPanel::OnPlaybackTick),
		1.0f / 60.0f);
}

void SLayerOverviewPanel::StopPlayback()
{
	bIsPlaying = false;
	if (PlaybackTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(PlaybackTickerHandle);
		PlaybackTickerHandle.Reset();
	}
}

void SLayerOverviewPanel::TogglePlayback()
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

bool SLayerOverviewPanel::OnPlaybackTick(float DeltaTime)
{
	if (!bIsPlaying || !Model.IsValid())
	{
		bIsPlaying = false;
		PlaybackTickerHandle.Reset();
		return false;
	}
	UPaperFlipbook* Flipbook = GetCurrentFlipbook();
	if (!Flipbook)
	{
		bIsPlaying = false;
		PlaybackTickerHandle.Reset();
		return false;
	}

	const FFlipbookTimingData Timing = FFlipbookTimingData::ReadFromFlipbook(Flipbook);
	if (Timing.TotalDurationSeconds <= 0.0f || Timing.FrameDurations.IsEmpty())
	{
		bIsPlaying = false;
		PlaybackTickerHandle.Reset();
		return false;
	}
	PlaybackPosition = FMath::Fmod(
		PlaybackPosition + FMath::Max(0.0f, DeltaTime),
		Timing.TotalDurationSeconds);

	int32 NewFrame = Timing.FrameDurations.Num() - 1;
	float FrameStart = 0.0f;
	for (int32 FrameIndex = 0; FrameIndex < Timing.FrameDurations.Num(); ++FrameIndex)
	{
		const float FrameEnd = FrameStart + Timing.GetFrameDurationSeconds(FrameIndex);
		if (PlaybackPosition < FrameEnd)
		{
			NewFrame = FrameIndex;
			break;
		}
		FrameStart = FrameEnd;
	}
	if (NewFrame != Model->GetSelectedFrameIndex())
	{
		Model->SetSelectedFrame(NewFrame);
	}
	return true;
}

int32 SLayerOverviewPanel::GetPreviewPaintItemCountForTests() const
{
	return PreviewCanvas.IsValid() ? PreviewCanvas->GetCachedPaintItemCountForTests() : 0;
}

int32 SLayerOverviewPanel::GetFrameCellCountForTests() const
{
	return FrameStripBox.IsValid() ? FrameStripBox->GetChildren()->Num() : 0;
}

FSlateColor SLayerOverviewPanel::GetKeyboardFocusIndicatorColor() const
{
	return HasAnyUserFocusOrFocusedDescendants()
		? FSlateColor(FLinearColor::White)
		: FSlateColor(FLinearColor::Transparent);
}

#undef LOCTEXT_NAMESPACE
