// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "PlaybackQueuePanel.h"
#include "CharacterProfileEditorModel.h"
#include "EditorCanvasUtils.h"
#include "SSpriteEditorDragDropWidgets.h"
#include "SlateShortcutUtils.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "PaperFlipbook.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Application/SlateApplication.h"
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
#include "EditorStyleSet.h"
#ifndef FAppStyle
#define FAppStyle FEditorStyle
#define GetAppStyleSetName GetStyleSetName
#endif
#else
#include "Styling/AppStyle.h"
#endif

#define LOCTEXT_NAMESPACE "PlaybackQueuePanel"

void SPlaybackQueuePanel::Construct(const FArguments& InArgs)
{
	Model = InArgs._Model;

	if (Model.IsValid())
	{
		Asset = Model->GetAsset();

		// Selection changes repaint the row highlight only — the model reconciles the queue cursor
		// onto the selected animation, so the "current entry" tint follows navigation from any tab.
		ModelFlipbookSelectionHandle = Model->OnFlipbookSelectionChanged.AddLambda([this](int32)
		{
			if (QueueListBox.IsValid())
				QueueListBox->Invalidate(EInvalidateWidgetReason::Paint);
		});

		ModelAssetDataChangedHandle = Model->OnAssetDataChanged.AddLambda([this]()
		{
			Asset = Model.IsValid() ? Model->GetAsset() : nullptr;
			RefreshQueueList();
		});

		ModelAssetExternallyModifiedHandle = Model->OnAssetExternallyModified.AddLambda([this]()
		{
			RefreshQueueList();
		});

		ModelQueueChangedHandle = Model->OnQueueChanged.AddLambda([this]()
		{
			RefreshQueueList();
		});

		ModelQueuePlaybackStateHandle = Model->OnQueuePlaybackStateChanged.AddLambda([this](bool bPlaying)
		{
			if (bPlaying && !PlaybackTickerHandle.IsValid())
			{
				PlaybackPosition = 0.0f;
				bPlaybackReversed = false;
				CachedPlaybackTiming = FFlipbookTimingData();
				PlaybackTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
					FTickerDelegate::CreateSP(this, &SPlaybackQueuePanel::OnQueuePlaybackTick));
			}
			else if (!bPlaying && PlaybackTickerHandle.IsValid())
			{
				FTSTicker::GetCoreTicker().RemoveTicker(PlaybackTickerHandle);
				PlaybackTickerHandle.Reset();
				PlaybackPosition = 0.0f;
				CachedPlaybackTiming = FFlipbookTimingData();
			}
			if (QueueListBox.IsValid())
				QueueListBox->Invalidate(EInvalidateWidgetReason::Paint);
		});
	}

	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4, 4, 4, 2)
		[
			BuildTransportBar()
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SSeparator)
		]

		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SAssignNew(QueueListBox, SVerticalBox)
			]
		]
	];

	RefreshQueueList();
}

SPlaybackQueuePanel::~SPlaybackQueuePanel()
{
	StopQueuePlayback();

	// The ticker holds an SP delegate to this widget; the state-change handler that would normally
	// remove it never runs when the tab is torn down mid-playback.
	if (PlaybackTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(PlaybackTickerHandle);
		PlaybackTickerHandle.Reset();
	}

	if (Model.IsValid())
	{
		Model->OnFlipbookSelectionChanged.Remove(ModelFlipbookSelectionHandle);
		Model->OnAssetDataChanged.Remove(ModelAssetDataChangedHandle);
		Model->OnAssetExternallyModified.Remove(ModelAssetExternallyModifiedHandle);
		Model->OnQueueChanged.Remove(ModelQueueChangedHandle);
		Model->OnQueuePlaybackStateChanged.Remove(ModelQueuePlaybackStateHandle);
	}
}

TSharedRef<SWidget> SPlaybackQueuePanel::BuildTransportBar()
{
	return SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text_Lambda([this]() -> FText
			{
				const int32 Count = Model.IsValid() ? Model->GetPlaybackQueue().Num() : 0;
				if (Count == 0) return LOCTEXT("QueueEmpty", "Queue");
				return FText::Format(LOCTEXT("QueueCount", "Queue ({0})"), FText::AsNumber(Count));
			})
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(2, 0)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ToolTipText(LOCTEXT("PlayStopTip", "Play / Stop queue (Space)"))
			.IsEnabled_Lambda([this]() { return Model.IsValid() && Model->IsQueueActive(); })
			.OnClicked_Lambda([this]()
			{
				ToggleQueuePlayback();
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Text_Lambda([this]() -> FText
				{
					return (Model.IsValid() && Model->IsQueuePlaying())
						? LOCTEXT("StopBtn", "Stop")
						: LOCTEXT("PlayBtn", "Play");
				})
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			]
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(2, 0)
		[
			SNew(SCheckBox)
			.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
			.ToolTipText(LOCTEXT("PingPongTip", "Ping-pong playback"))
			.IsChecked_Lambda([this]() { return bPingPongPlayback ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState) { bPingPongPlayback = (NewState == ECheckBoxState::Checked); })
			[
				SNew(STextBlock)
				.Text(LOCTEXT("PingPongLabel", "P-P"))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
			]
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(2, 0)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ToolTipText(LOCTEXT("ClearQueueTip", "Clear queue"))
			.IsEnabled_Lambda([this]() { return Model.IsValid() && Model->IsQueueActive(); })
			.OnClicked_Lambda([this]()
			{
				if (Model.IsValid()) Model->ClearQueue();
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ClearBtn", "Clear"))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			]
		];
}

void SPlaybackQueuePanel::RefreshQueueList()
{
	if (!QueueListBox.IsValid() || !Model.IsValid() || !Asset.IsValid()) return;
	QueueListBox->ClearChildren();

	const TArray<int32>& Queue = Model->GetPlaybackQueue();

	class SQueueDropZone : public SBorder
	{
	public:
		TFunction<void(const TArray<int32>&)> OnFlipbooksDropped;

		virtual FReply OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override
		{
			if (DragDropEvent.GetOperationAs<FQueueDragDropOp>().IsValid()) return FReply::Handled();
			return FReply::Unhandled();
		}
		virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override
		{
			TSharedPtr<FQueueDragDropOp> Op = DragDropEvent.GetOperationAs<FQueueDragDropOp>();
			if (Op.IsValid() && Op->IsFromFlipbookList() && OnFlipbooksDropped)
			{
				OnFlipbooksDropped(Op->FlipbookIndices);
				return FReply::Handled();
			}
			return FReply::Unhandled();
		}
	};

	const int32 QueueCount = Queue.Num();
	auto MakeDropZone = [this, QueueCount]() -> TSharedRef<SWidget>
	{
		TSharedRef<SQueueDropZone> Zone = SNew(SQueueDropZone)
			.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.DarkGroupBorder"))
			.Padding(FMargin(8, QueueCount == 0 ? 16.0f : 4.0f))
			[
				SNew(STextBlock)
				.Text(QueueCount == 0
					? LOCTEXT("QueueEmptyHint", "Drag animations here, or double-click one in the Navigator")
					: LOCTEXT("QueueDropHint", "Drop here to add"))
				.Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.4f, 0.4f, 0.4f)))
				.Justification(ETextJustify::Center)
				.AutoWrapText(true)
			];

		Zone->OnFlipbooksDropped = [this](const TArray<int32>& Indices)
		{
			if (!Model.IsValid()) return;
			for (int32 Idx : Indices) Model->AddToQueue(Idx);
		};

		return Zone;
	};

	if (Queue.Num() == 0)
	{
		QueueListBox->AddSlot().AutoHeight()[MakeDropZone()];
		return;
	}

	for (int32 QueueIdx = 0; QueueIdx < Queue.Num(); ++QueueIdx)
	{
		const int32 AnimIdx = Queue[QueueIdx];
		if (!Asset->Flipbooks.IsValidIndex(AnimIdx)) continue;

		const FFlipbookProfileEntry& Anim = Asset->Flipbooks[AnimIdx];
		FString AnimName = Anim.Identity.FlipbookName;
		UPaperFlipbook* LoadedFlipbook = Anim.Identity.Flipbook.Get();

		TSharedRef<SQueueEntryDragDropWrapper> EntryWrapper = SNew(SQueueEntryDragDropWrapper)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
			.BorderBackgroundColor_Lambda([this, QueueIdx]()
			{
				if (!Model.IsValid()) return FLinearColor(0.03f, 0.03f, 0.03f, 1.0f);
				const bool bIsCurrentEntry = (QueueIdx == Model->GetPlaybackQueueIndex());
				if (Model->IsQueuePlaying() && bIsCurrentEntry) return FLinearColor(0.15f, 0.45f, 0.20f, 1.0f);
				if (bIsCurrentEntry) return FLinearColor(0.15f, 0.35f, 0.55f, 1.0f);
				return FLinearColor(0.03f, 0.03f, 0.03f, 1.0f);
			})
			.Padding(FMargin(6, 3))
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0, 0, 4, 0)
				[
					SNew(STextBlock)
					.Text(FText::AsNumber(QueueIdx + 1))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
					.MinDesiredWidth(12)
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 6, 0)
				[
					SNew(SBox)
					.WidthOverride(32)
					.HeightOverride(32)
					[
						LoadedFlipbook
							? StaticCastSharedRef<SWidget>(SNew(SFlipbookThumbnail).Flipbook(LoadedFlipbook))
							: StaticCastSharedRef<SWidget>(SNew(SBorder)
								.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.DarkGroupBorder"))
								.HAlign(HAlign_Center).VAlign(VAlign_Center)
								[
									SNew(STextBlock)
									.Text(LOCTEXT("NoFBQ", "?"))
									.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
								])
					]
				]

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::FromString(AnimName))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(4, 0, 0, 0)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.ToolTipText(LOCTEXT("RemoveFromQueueTip", "Remove from queue"))
					.OnClicked_Lambda([this, QueueIdx]()
					{
						if (Model.IsValid()) Model->RemoveFromQueue(QueueIdx);
						return FReply::Handled();
					})
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("x")))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.3f, 0.3f)))
					]
				]
			]
		];

		EntryWrapper->QueueIndex = QueueIdx;
		EntryWrapper->FlipbookIndex = AnimIdx;
		EntryWrapper->FlipbookName = AnimName;

		EntryWrapper->OnClickedFunc = [this, QueueIdx]()
		{
			if (!Model.IsValid()) return;
			const TArray<int32>& Q = Model->GetPlaybackQueue();
			if (!Q.IsValidIndex(QueueIdx)) return;
			// Cursor first, then selection: SetSelectedFlipbook reconciles the cursor onto the
			// selection, which would snap a duplicated animation back to its first occurrence.
			Model->SetPlaybackQueueIndex(QueueIdx);
			Model->SetSelectedFlipbook(Q[QueueIdx]);
			Model->SetPlaybackQueueIndex(QueueIdx);
		};

		EntryWrapper->OnRightClickFunc = [this, QueueIdx]()
		{
			ShowQueueEntryContextMenu(QueueIdx);
		};

		EntryWrapper->OnQueueReorderFunc = [this](int32 From, int32 To)
		{
			if (Model.IsValid()) Model->ReorderQueueEntry(From, To);
		};

		EntryWrapper->OnAnimsDroppedFunc = [this](const TArray<int32>& FlipbookIndices, int32 InsertAt)
		{
			if (!Model.IsValid()) return;
			for (int32 Idx : FlipbookIndices) Model->AddToQueue(Idx);
		};

		QueueListBox->AddSlot().AutoHeight()[EntryWrapper];
	}

	QueueListBox->AddSlot().AutoHeight()[MakeDropZone()];
}

void SPlaybackQueuePanel::ShowQueueEntryContextMenu(int32 QueueIndex)
{
	FMenuBuilder MenuBuilder(true, nullptr);

	if (QueueIndex > 0)
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("MoveUp", "Move Up"),
			FText::GetEmpty(), FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([this, QueueIndex]()
			{
				if (Model.IsValid()) Model->ReorderQueueEntry(QueueIndex, QueueIndex - 1);
			}))
		);
	}

	if (Model.IsValid() && QueueIndex < Model->GetPlaybackQueue().Num() - 1)
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("MoveDown", "Move Down"),
			FText::GetEmpty(), FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([this, QueueIndex]()
			{
				if (Model.IsValid()) Model->ReorderQueueEntry(QueueIndex, QueueIndex + 2);
			}))
		);
	}

	MenuBuilder.AddMenuEntry(
		LOCTEXT("RemoveFromQueue", "Remove"),
		FText::GetEmpty(), FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this, QueueIndex]()
		{
			if (Model.IsValid()) Model->RemoveFromQueue(QueueIndex);
		}))
	);

	FSlateApplication::Get().PushMenu(
		SharedThis(this),
		FWidgetPath(),
		MenuBuilder.MakeWidget(),
		FSlateApplication::Get().GetCursorPos(),
		FPopupTransitionEffect::ContextMenu);
}

// ==========================================
// Playback
// ==========================================

void SPlaybackQueuePanel::ToggleQueuePlayback()
{
	if (!Model.IsValid()) return;
	if (Model->IsQueuePlaying())
	{
		StopQueuePlayback();
	}
	else
	{
		StartQueuePlayback();
	}
}

void SPlaybackQueuePanel::StartQueuePlayback()
{
	if (!Model.IsValid() || !Model->IsQueueActive()) return;
	if (Model->IsQueuePlaying()) return;

	// Resume from wherever the designer navigated to rather than the stale cursor: the model
	// reconciles the cursor onto the selection, so playback picks up at the animation on screen.
	Model->SyncQueueIndexToSelection();

	const TArray<int32>& Queue = Model->GetPlaybackQueue();
	const int32 QueueIdx = Model->GetPlaybackQueueIndex();
	if (Queue.IsValidIndex(QueueIdx) && Asset.IsValid())
	{
		const int32 AnimIdx = Queue[QueueIdx];
		if (Asset->Flipbooks.IsValidIndex(AnimIdx))
		{
			Model->SetSelectedFlipbook(AnimIdx);
		}
	}

	Model->SetQueuePlaying(true);
}

void SPlaybackQueuePanel::StopQueuePlayback()
{
	if (Model.IsValid() && Model->IsQueuePlaying())
	{
		Model->SetQueuePlaying(false);
	}
}

bool SPlaybackQueuePanel::OnQueuePlaybackTick(float DeltaTime)
{
	if (!Model.IsValid() || !Asset.IsValid() || !Model->IsQueueActive())
	{
		StopQueuePlayback();
		return false;
	}

	const TArray<int32>& Queue = Model->GetPlaybackQueue();
	int32 QueueIdx = Model->GetPlaybackQueueIndex();

	while (QueueIdx < Queue.Num())
	{
		const int32 AnimIdx = Queue[QueueIdx];
		if (Asset->Flipbooks.IsValidIndex(AnimIdx) && !Asset->Flipbooks[AnimIdx].Identity.Flipbook.IsNull()) break;
		QueueIdx++;
		PlaybackPosition = 0.0f;
		CachedPlaybackTiming = FFlipbookTimingData();
	}

	if (QueueIdx >= Queue.Num())
	{
		QueueIdx = 0;
		PlaybackPosition = 0.0f;
		bool bFound = false;
		for (int32 i = 0; i < Queue.Num(); i++)
		{
			if (Asset->Flipbooks.IsValidIndex(Queue[i]) && !Asset->Flipbooks[Queue[i]].Identity.Flipbook.IsNull())
			{
				QueueIdx = i;
				bFound = true;
				break;
			}
		}
		if (!bFound) return true;
		CachedPlaybackTiming = FFlipbookTimingData();
	}

	Model->SetPlaybackQueueIndex(QueueIdx);

	const int32 AnimIdx = Queue[QueueIdx];
	const FFlipbookProfileEntry& Anim = Asset->Flipbooks[AnimIdx];

	if (CachedPlaybackTiming.TotalDurationSeconds <= 0.0f)
	{
		UPaperFlipbook* FB = Anim.Identity.Flipbook.LoadSynchronous();
		if (!FB)
		{
			Model->SetPlaybackQueueIndex((QueueIdx + 1) % Queue.Num());
			PlaybackPosition = 0.0f;
			return true;
		}
		CachedPlaybackTiming = FFlipbookTimingData::ReadFromFlipbook(FB);
		if (CachedPlaybackTiming.TotalDurationSeconds <= 0.0f)
		{
			Model->SetPlaybackQueueIndex((QueueIdx + 1) % Queue.Num());
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
				const float Overflow = FMath::Abs(PlaybackPosition);
				const int32 NextQIdx = (QueueIdx + 1) % Queue.Num();
				PlaybackPosition = Overflow;
				bPlaybackReversed = false;
				CachedPlaybackTiming = FFlipbookTimingData();
				const TArray<int32>& Q = Model->GetPlaybackQueue();
				if (Q.IsValidIndex(NextQIdx))
				{
					// Selection first would reconcile the cursor onto a duplicate's first slot;
					// re-assert after so the queue advances entry by entry.
					Model->SetPlaybackQueueIndex(NextQIdx);
					Model->SetSelectedFlipbook(Q[NextQIdx]);
					Model->SetPlaybackQueueIndex(NextQIdx);
					Model->SetSelectedFrame(0);
				}
				RefreshQueueList();
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
		if (PlaybackPosition >= CachedPlaybackTiming.TotalDurationSeconds)
		{
			const float Overflow = PlaybackPosition - CachedPlaybackTiming.TotalDurationSeconds;
			const int32 NextQIdx = (QueueIdx + 1) % Queue.Num();
			PlaybackPosition = Overflow;
			CachedPlaybackTiming = FFlipbookTimingData();
			const TArray<int32>& Q = Model->GetPlaybackQueue();
			if (Q.IsValidIndex(NextQIdx))
			{
				Model->SetPlaybackQueueIndex(NextQIdx);
				Model->SetSelectedFlipbook(Q[NextQIdx]);
				Model->SetPlaybackQueueIndex(NextQIdx);
				Model->SetSelectedFrame(0);
			}
			RefreshQueueList();
			return true;
		}
	}

	if (AnimIdx != Model->GetSelectedFlipbookIndex())
	{
		Model->SetSelectedFlipbook(AnimIdx);
		Model->SetPlaybackQueueIndex(QueueIdx);
		Model->SetSelectedFrame(0);
	}

	const int32 NewFrame = FrameIndexFromPlaybackPosition(CachedPlaybackTiming, PlaybackPosition);
	if (NewFrame != Model->GetSelectedFrameIndex())
	{
		Model->SetSelectedFrame(NewFrame);
	}

	return true;
}

int32 SPlaybackQueuePanel::FrameIndexFromPlaybackPosition(const FFlipbookTimingData& Timing, float Position) const
{
	float Acc = 0.0f;
	for (int32 i = 0; i < Timing.FrameDurations.Num(); i++)
	{
		const float Dur = Timing.GetFrameDurationSeconds(i);
		if (Position < Acc + Dur) return i;
		Acc += Dur;
	}
	return FMath::Max(0, Timing.FrameDurations.Num() - 1);
}

FReply SPlaybackQueuePanel::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (Paper2DPlusEditor::SlateShortcutUtils::ShouldIgnoreShortcutForFocusedWidget())
	{
		return FReply::Unhandled();
	}

	if (InKeyEvent.GetKey() == EKeys::SpaceBar && !InKeyEvent.IsControlDown())
	{
		if (Model.IsValid() && Model->IsQueueActive())
		{
			ToggleQueuePlayback();
			return FReply::Handled();
		}
	}
	return FReply::Unhandled();
}

#if WITH_DEV_AUTOMATION_TESTS
int32 SPlaybackQueuePanel::GetQueueRowCountForTests() const
{
	// The trailing drop zone is always present and is not a queue entry.
	if (!QueueListBox.IsValid()) return 0;
	const int32 SlotCount = QueueListBox->NumSlots();
	return FMath::Max(0, SlotCount - 1);
}
#endif

#undef LOCTEXT_NAMESPACE
