// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "FrameEventEditor.h"
#include "SFrameEventPreviewCanvas.h"
#include "SFrameEventTimelineTrack.h"
#include "FrameEvents/Paper2DPlusFrameEventBase.h"
#include "FrameEvents/Paper2DPlusFrameEvent.h"
#include "FrameEvents/Paper2DPlusFrameEventState.h"
#include "FrameEvents/Paper2DPlusSpawnEffectFrameEvent.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "EditorCanvasUtils.h"
#include "AnimationTimeline.h"
#include "PaperFlipbook.h"
#include "PaperSprite.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "ScopedTransaction.h"
#include "PropertyEditorModule.h"
#include "Modules/ModuleManager.h"
#include "ClassViewerModule.h"
#include "ClassViewerFilter.h"
#include "Containers/Ticker.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "AssetToolsModule.h"
#include "Factories/BlueprintFactory.h"
#include "Engine/Blueprint.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/UObjectIterator.h"

/** SFrameEventEditor — Frame events tab: timeline track, event CRUD, preview canvas, and event type selection. */

#define LOCTEXT_NAMESPACE "FrameEventEditor"

// ─── Playback time-to-frame helper ──────────────────────────────────────────

namespace FrameEventEditorInternal
{
static int32 TimeToFrameIndex(const FFlipbookTimingData& Timing, double Time)
{
	if (Timing.TotalFrames <= 0 || Timing.TotalDurationSeconds <= 0.0f) return 0;
	for (int32 i = Timing.TotalFrames - 1; i >= 0; i--)
	{
		if (Time >= Timing.GetFrameStartTime(i))
		{
			return i;
		}
	}
	return 0;
}
}

// ─── Class picker filter for frame event subclasses ──────────────────────────

class FFrameEventClassFilter : public IClassViewerFilter
{
public:
	virtual bool IsClassAllowed(const FClassViewerInitializationOptions& Options,
		const UClass* InClass, TSharedRef<FClassViewerFilterFuncs> FilterFuncs) override
	{
		// Accept any loaded subclass of the frame event base that isn't
		// abstract/deprecated/hidden. We deliberately do NOT filter on
		// "BlueprintType" / "IsBlueprintBase" metadata here — UClass::HasMetaData
		// doesn't walk the parent chain on BlueprintGeneratedClass, so BP
		// subclasses of Blueprintable C++ parents silently fail that check
		// once they're loaded into memory. Without this symptom the picker
		// shows the user's events on the first editor launch (the unloaded
		// path below omits the metadata check and works fine) and then
		// goes empty after the BP classes are touched. Keep this in sync
		// with IsUnloadedClassAllowed.
		return InClass &&
			InClass->IsChildOf(UPaper2DPlusFrameEventBase::StaticClass()) &&
			!InClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_Hidden);
	}

	virtual bool IsUnloadedClassAllowed(const FClassViewerInitializationOptions& Options,
		const TSharedRef<const IUnloadedBlueprintData> InUnloadedClass,
		TSharedRef<FClassViewerFilterFuncs> FilterFuncs) override
	{
		return InUnloadedClass->IsChildOf(UPaper2DPlusFrameEventBase::StaticClass()) &&
			!InUnloadedClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_Hidden);
	}
};

// ─── SFrameEventEditor ─────────────────────────────────────────────────────

void SFrameEventEditor::Construct(const FArguments& InArgs)
{
	Asset = InArgs._Asset;
	BuildFlipbookListFunc = InArgs._BuildFlipbookListFunc;
	ActiveEditorEffects = MakeShared<TArray<FEditorEffectPreview>>();

	GEditor->RegisterForUndo(this);

	// Details panel for selected event
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsArgs;
	DetailsArgs.bAllowSearch = false;
	DetailsArgs.bHideSelectionTip = true;
	DetailsArgs.bShowOptions = false;
	DetailsArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	EventDetailsView = PropertyModule.CreateDetailView(DetailsArgs);
	EventDetailsView->OnFinishedChangingProperties().AddLambda([this](const FPropertyChangedEvent&)
	{
		// Re-layout timeline when event properties change (e.g. EffectFlipbook assigned)
		if (TimelineTrack.IsValid()) TimelineTrack->Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
		if (FrameStripBox.IsValid()) FrameStripBox->Invalidate(EInvalidateWidgetReason::Paint);
		RefreshEventList();
	});

	ChildSlot
	[
		// Main content: flipbook list | (toolbar + canvas + frame strip) | event list + details
		SNew(SSplitter)
		.Orientation(Orient_Horizontal)

			// Left: Flipbook list (18%)
			+ SSplitter::Slot()
			.Value(0.18f)
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(4, 4, 4, 4)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("FlipbooksHeader", "Flipbooks"))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
				]

				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					SNew(SScrollBox)
					+ SScrollBox::Slot()
					[
						SAssignNew(FlipbookListBox, SVerticalBox)
					]
				]
			]

			// Center: Toolbar + Canvas + Frame strip (50%)
			+ SSplitter::Slot()
			.Value(0.50f)
			[
				SNew(SVerticalBox)

				// Toolbar (moved here so left flipbook list stays top-flush)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 0, 0, 4)
				[
					BuildToolbar()
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(4, 2, 4, 0)
				[
					SNew(STextBlock)
					.Text_Lambda([this]() {
						if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex))
							return FText::FromString(TEXT("No Flipbook"));
						const FFlipbookProfileEntry& Entry = Asset->Flipbooks[SelectedFlipbookIndex];
						UPaperFlipbook* FB = Entry.Identity.Flipbook.LoadSynchronous();
						int32 FrameCount = FB ? FB->GetNumKeyFrames() : 0;
						return FText::Format(LOCTEXT("EventFlipbookTitleFmt", "{0}  Frame {1}/{2}"),
							FText::FromString(Entry.Identity.FlipbookName),
							FText::AsNumber(SelectedFrameIndex + 1),
							FText::AsNumber(FrameCount));
					})
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
				]

				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					SAssignNew(PreviewCanvasWidget, SFrameEventPreviewCanvas)
					.Flipbook_Lambda([this]() -> UPaperFlipbook* { return GetSelectedFlipbook(); })
					.FrameIndex_Lambda([this]() { return SelectedFrameIndex; })
					.Asset(Asset)
					.FlipbookIndex_Lambda([this]() { return SelectedFlipbookIndex; })
					.ActiveEditorEffects(ActiveEditorEffects)
					.PlaybackTime_Lambda([this]() { return PlaybackTime; })
					.IsPlaying_Lambda([this]() { return bIsPlaying; })
					.SelectedEventIndex_Lambda([this]() { return SelectedEventIndex; })
				]

				// Frame strip + Timeline track (unified horizontal scroll)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SScrollBox)
					.Orientation(Orient_Horizontal)
					+ SScrollBox::Slot()
					[
						SNew(SVerticalBox)

						// Frame strip row (flush left — labels are inline in timeline)
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SAssignNew(FrameStripBox, SHorizontalBox)
						]

						// Timeline track — capped at ~4 tall events (264px); scrolls vertically beyond that
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SBox)
							.MaxDesiredHeight(264.0f)
							[
								SNew(SScrollBox)
								.Orientation(Orient_Vertical)
								+ SScrollBox::Slot()
								[
									SAssignNew(TimelineTrack, SFrameEventTimelineTrack)
									.Asset(Asset)
									.SelectedFlipbookIndex_Lambda([this]() { return SelectedFlipbookIndex; })
									.SelectedEventIndex_Lambda([this]() { return SelectedEventIndex; })
									.SelectedFrameIndex_Lambda([this]() { return SelectedFrameIndex; })
								]
							]
						]
					]
				]
			]

			// Right: Event list + details (32%)
			+ SSplitter::Slot()
			.Value(0.32f)
			[
				SNew(SVerticalBox)

				// Event list toolbar (Add/Remove)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(4)
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(0, 0, 4, 0)
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
						.OnClicked_Lambda([this]()
						{
							ShowAddEventPicker();
							return FReply::Handled();
						})
						[
							SNew(STextBlock)
							.Text(LOCTEXT("AddFrameEvent", "+ Add Event"))
						]
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
						.IsEnabled_Lambda([this]() { return SelectedEventIndex != INDEX_NONE; })
						.OnClicked_Lambda([this]()
						{
							if (SelectedEventIndex != INDEX_NONE)
							{
								RemoveEvent(SelectedEventIndex);
							}
							return FReply::Handled();
						})
						[
							SNew(STextBlock)
							.Text(LOCTEXT("RemoveFrameEvent", "Remove"))
						]
					]
				]

				// Event list
				+ SVerticalBox::Slot()
				.FillHeight(0.5f)
				[
					SNew(SScrollBox)
					+ SScrollBox::Slot()
					[
						SAssignNew(EventListBox, SVerticalBox)
					]
				]

				// Details panel
				+ SVerticalBox::Slot()
				.FillHeight(0.5f)
				[
					EventDetailsView.ToSharedRef()
				]
			]
	];

	// Wire preview canvas drag delegates
	if (PreviewCanvasWidget.IsValid())
	{
		PreviewCanvasWidget->OnEffectDragStarted.BindLambda([this]()
		{
			BeginTransaction(LOCTEXT("DragEffectOffset", "Drag Effect Offset"));
		});

		PreviewCanvasWidget->OnEffectOffsetChanged.BindLambda([this](FVector2D NewOffset)
		{
			int32 SelEvIdx = SelectedEventIndex;
			if (SelEvIdx == INDEX_NONE || !Asset.IsValid()) return;
			int32 FBIdx = SelectedFlipbookIndex;
			if (!Asset->Flipbooks.IsValidIndex(FBIdx)) return;
			auto& Events = Asset->Flipbooks[FBIdx].FrameEventData.FrameEvents;
			if (!Events.IsValidIndex(SelEvIdx)) return;

			if (UPaper2DPlusSpawnEffectFrameEvent* SpawnEvent = Cast<UPaper2DPlusSpawnEffectFrameEvent>(Events[SelEvIdx]))
			{
				SpawnEvent->Offset = NewOffset;
				if (EventDetailsView.IsValid())
				{
					EventDetailsView->ForceRefresh();
				}
			}
		});

		PreviewCanvasWidget->OnEffectDragEnded.BindLambda([this]()
		{
			EndTransaction();
		});
	}

	// Wire timeline track delegates
	if (TimelineTrack.IsValid())
	{
		TimelineTrack->OnFrameClicked.BindLambda([this](int32 FrameIdx)
		{
			OnFrameClicked(FrameIdx);
		});

		TimelineTrack->OnEventSelected.BindLambda([this](int32 EventIdx)
		{
			OnEventSelected(EventIdx);
		});

		TimelineTrack->OnEventDragStarted.BindLambda([this]()
		{
			BeginTransaction(LOCTEXT("MoveFrameEvent", "Move Frame Event"));
		});

		TimelineTrack->OnEventDragEnded.BindLambda([this]()
		{
			EndTransaction();
			RefreshEventList();
			if (FrameStripBox.IsValid()) FrameStripBox->Invalidate(EInvalidateWidgetReason::Paint);
			if (TimelineTrack.IsValid()) TimelineTrack->Invalidate(EInvalidateWidgetReason::Paint);
		});

		TimelineTrack->OnEventFrameChanged.BindLambda([this](int32 EventIdx, int32 NewFrame)
		{
			TArray<TObjectPtr<UPaper2DPlusFrameEventBase>>* Events = GetFrameEventsArray();
			if (!Events || !Events->IsValidIndex(EventIdx)) return;

			UPaper2DPlusFrameEventBase* Event = (*Events)[EventIdx];
			if (!Event) return;

			if (Asset.IsValid()) Asset->Modify();

			if (UPaper2DPlusFrameEventState* Ranged = Cast<UPaper2DPlusFrameEventState>(Event))
			{
				Ranged->StartFrame = NewFrame;
			}
			else if (UPaper2DPlusFrameEvent* OneShot = Cast<UPaper2DPlusFrameEvent>(Event))
			{
				OneShot->TriggerFrame = NewFrame;
			}
		});

		TimelineTrack->OnEventDurationChanged.BindLambda([this](int32 EventIdx, int32 NewFrameCount)
		{
			TArray<TObjectPtr<UPaper2DPlusFrameEventBase>>* Events = GetFrameEventsArray();
			if (!Events || !Events->IsValidIndex(EventIdx)) return;

			UPaper2DPlusFrameEventBase* Event = (*Events)[EventIdx];
			if (!Event) return;

			if (Asset.IsValid()) Asset->Modify();

			if (UPaper2DPlusFrameEventState* Ranged = Cast<UPaper2DPlusFrameEventState>(Event))
			{
				Ranged->FrameCount = FMath::Max(1, NewFrameCount);
			}
		});

		TimelineTrack->OnEventRemoved.BindLambda([this](int32 EventIdx)
		{
			RemoveEvent(EventIdx);
		});
	}

	RefreshAll();
}

SFrameEventEditor::~SFrameEventEditor()
{
	StopPlayback();
	if (GEditor)
	{
		GEditor->UnregisterForUndo(this);
	}
}

// ─── UNDO / REDO ────────────────────────────────────────────────────────────

void SFrameEventEditor::PostUndo(bool bSuccess) { StopPlayback(); bNeedsRefresh = true; }
void SFrameEventEditor::PostRedo(bool bSuccess) { StopPlayback(); bNeedsRefresh = true; }

// ─── Transaction helpers ─────────────────────────────────────────────────────

void SFrameEventEditor::BeginTransaction(const FText& Description)
{
	ActiveTransaction = MakeUnique<FScopedTransaction>(Description);
	if (Asset.IsValid())
	{
		Asset->Modify();
	}
}

void SFrameEventEditor::EndTransaction()
{
	ActiveTransaction.Reset();
}

// ─── TOOLBAR ─────────────────────────────────────────────────────────────────

TSharedRef<SWidget> SFrameEventEditor::BuildToolbar()
{
	// Empty toolbar — playback via Space, frame info via standardized title
	return SNullWidget::NullWidget;
}

// ─── FLIPBOOK LIST ───────────────────────────────────────────────────────────

void SFrameEventEditor::RefreshFlipbookList()
{
	if (!FlipbookListBox.IsValid() || !Asset.IsValid()) return;
	FlipbookListBox->ClearChildren();

	if (BuildFlipbookListFunc)
	{
		BuildFlipbookListFunc(FlipbookListBox, [this](int32 i) -> TSharedRef<SWidget>
		{
			if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(i)) return SNullWidget::NullWidget;

			const FFlipbookProfileEntry& Anim = Asset->Flipbooks[i];
			UPaperFlipbook* LoadedFlipbook = !Anim.Identity.Flipbook.IsNull() ? Anim.Identity.Flipbook.LoadSynchronous() : nullptr;
			const int32 EventCount = Anim.FrameEventData.FrameEvents.Num();

			return SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "NoBorder")
				.OnClicked_Lambda([this, i]() { SetSelectedFlipbook(i); return FReply::Handled(); })
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					.BorderBackgroundColor_Lambda([this, i]()
					{
						return i == SelectedFlipbookIndex
							? FLinearColor(0.15f, 0.35f, 0.55f, 1.0f)
							: FLinearColor(0.03f, 0.03f, 0.03f, 1.0f);
					})
					.Padding(FMargin(8, 6))
					[
						SNew(SHorizontalBox)

						// Thumbnail
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0, 0, 8, 0)
						[
							SNew(SBox)
							.WidthOverride(44)
							.HeightOverride(44)
							[
								LoadedFlipbook
									? StaticCastSharedRef<SWidget>(SNew(SFlipbookThumbnail).Flipbook(LoadedFlipbook))
									: StaticCastSharedRef<SWidget>(SNew(SBorder)
										.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
										.HAlign(HAlign_Center).VAlign(VAlign_Center)
										[
											SNew(STextBlock)
											.Text(LOCTEXT("NoFBList", "No FB"))
											.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
											.ColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.55f, 0.55f)))
										])
							]
						]

						// Name + event count badge
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.VAlign(VAlign_Center)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(STextBlock)
								.Text(FText::FromString(Anim.Identity.FlipbookName))
								.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
							]
							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(STextBlock)
								.Visibility(EventCount > 0 ? EVisibility::Visible : EVisibility::Collapsed)
								.Text(FText::Format(LOCTEXT("EventCountBadge", "{0} event(s)"), FText::AsNumber(EventCount)))
								.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
								.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
							]
						]
					]
				];
		});
	}
}

// ─── FRAME STRIP ─────────────────────────────────────────────────────────────

void SFrameEventEditor::RefreshFrameStrip()
{
	if (!FrameStripBox.IsValid()) return;
	FrameStripBox->ClearChildren();

	UPaperFlipbook* FB = GetSelectedFlipbook();
	if (!FB || FB->GetNumKeyFrames() == 0) return;

	for (int32 i = 0; i < FB->GetNumKeyFrames(); i++)
	{
		UPaperSprite* Sprite = FB->GetKeyFrameChecked(i).Sprite;

		FFrameStripCellArgs CellArgs;
		CellArgs.Sprite = Sprite;
		CellArgs.FrameIndex = i;
		CellArgs.IsSelected = [this, i]() { return i == SelectedFrameIndex; };
		CellArgs.OnMouseButtonDown = [this, i](const FPointerEvent& Event) -> FReply
		{
			if (Event.GetEffectingButton() == EKeys::LeftMouseButton)
			{
				OnFrameClicked(i);
				return FReply::Handled();
			}
			return FReply::Unhandled();
		};

		// Color bar: Green for one-shot trigger, Blue for ranged span, Transparent otherwise
		CellArgs.bShowBottomBar = true;
		CellArgs.BottomBarColor = TAttribute<FLinearColor>::CreateLambda([this, i]()
		{
			TArray<TObjectPtr<UPaper2DPlusFrameEventBase>>* Events = GetFrameEventsArray();
			if (!Events) return FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);

			for (const TObjectPtr<UPaper2DPlusFrameEventBase>& Event : *Events)
			{
				if (!Event) continue;

				// Check one-shot events
				if (const UPaper2DPlusFrameEvent* OneShot = Cast<UPaper2DPlusFrameEvent>(Event))
				{
					if (OneShot->TriggerFrame == i)
					{
						return FLinearColor(0.3f, 0.8f, 0.3f); // Green
					}
				}

				// Check ranged events
				if (const UPaper2DPlusFrameEventState* Ranged = Cast<UPaper2DPlusFrameEventState>(Event))
				{
					if (Ranged->ContainsFrame(i))
					{
						return FLinearColor(0.3f, 0.5f, 0.9f); // Blue
					}
				}
			}

			return FLinearColor(0.0f, 0.0f, 0.0f, 0.0f); // Transparent
		});

		FrameStripBox->AddSlot()
		.AutoWidth()
		[
			FFrameStripCellUtils::Build(CellArgs)
		];
	}
}

// ─── EVENT LIST ──────────────────────────────────────────────────────────────

void SFrameEventEditor::RefreshEventList()
{
	if (!EventListBox) return;
	EventListBox->ClearChildren();

	TArray<TObjectPtr<UPaper2DPlusFrameEventBase>>* Events = GetFrameEventsArray();
	if (!Events || Events->Num() == 0)
	{
		EventListBox->AddSlot()
		.Padding(8)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("NoEvents", "No frame events. Click '+ Add Event' to create one."))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
			.AutoWrapText(true)
		];
		return;
	}

	for (int32 i = 0; i < Events->Num(); ++i)
	{
		UPaper2DPlusFrameEventBase* Event = (*Events)[i];
		if (!Event) continue;

		const FString EventClassName = Event->GetClass()->GetDisplayNameText().ToString();
		FString EventLabel = Event->DebugName.IsNone()
			? FString::Printf(TEXT("[%d] %s"), i, *EventClassName)
			: FString::Printf(TEXT("[%d] %s (%s)"), i, *Event->DebugName.ToString(), *EventClassName);

		// Show trigger frame for one-shot events
		if (const UPaper2DPlusFrameEvent* OneShot = Cast<UPaper2DPlusFrameEvent>(Event))
		{
			EventLabel += FString::Printf(TEXT(" @ frame %d"), OneShot->TriggerFrame);
		}
		else if (const UPaper2DPlusFrameEventState* Ranged = Cast<UPaper2DPlusFrameEventState>(Event))
		{
			EventLabel += FString::Printf(TEXT(" [%d-%d]"), Ranged->StartFrame, Ranged->StartFrame + Ranged->FrameCount - 1);
		}

		const int32 EventIdx = i;

		EventListBox->AddSlot()
		.AutoHeight()
		.Padding(2)
		[
			SNew(SBorder)
			.BorderBackgroundColor_Lambda([this, EventIdx]()
			{
				return EventIdx == SelectedEventIndex
					? FLinearColor(0.15f, 0.45f, 0.75f, 1.0f)
					: FLinearColor(0.05f, 0.05f, 0.05f, 1.0f);
			})
			.Padding(6)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0, 0, 6, 0)
				[
					SNew(SBox)
					.WidthOverride(12)
					.HeightOverride(12)
					[
						SNew(SBorder)
						.BorderBackgroundColor(Event->Color)
					]
				]

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::FromString(EventLabel))
				]
			]
			.OnMouseButtonDown_Lambda([this, EventIdx](const FGeometry&, const FPointerEvent& MouseEvent)
			{
				if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
				{
					OnEventSelected(EventIdx);
					return FReply::Handled();
				}
				return FReply::Unhandled();
			})
		];
	}
}

// ─── DETAILS PANEL ──────────────────────────────────────────────────────────

void SFrameEventEditor::RefreshDetailsPanel()
{
	if (!EventDetailsView) return;

	TArray<TObjectPtr<UPaper2DPlusFrameEventBase>>* Events = GetFrameEventsArray();
	if (Events && Events->IsValidIndex(SelectedEventIndex))
	{
		UPaper2DPlusFrameEventBase* Event = (*Events)[SelectedEventIndex];
		if (Event)
		{
			EventDetailsView->SetObject(Event);
			return;
		}
	}

	EventDetailsView->SetObject(nullptr);
}

// ─── EXTERNAL CONTROL ────────────────────────────────────────────────────────

void SFrameEventEditor::SetSelectedFlipbook(int32 FlipbookIndex)
{
	if (SelectedFlipbookIndex == FlipbookIndex) return;
	StopPlayback();
	SelectedFlipbookIndex = FlipbookIndex;
	SelectedFrameIndex = 0;
	SelectedEventIndex = INDEX_NONE;
	RefreshFrameStrip();
	RefreshEventList();
	RefreshDetailsPanel();
	OnFlipbookSelectedInList.ExecuteIfBound(FlipbookIndex);
}

void SFrameEventEditor::RefreshAll()
{
	bNeedsRefresh = false;
	RefreshFlipbookList();
	RefreshFrameStrip();
	RefreshEventList();
	RefreshDetailsPanel();
	if (TimelineTrack.IsValid()) TimelineTrack->Invalidate(EInvalidateWidgetReason::Paint);
}

// ─── PLAYBACK ────────────────────────────────────────────────────────────────

void SFrameEventEditor::TogglePlayback()
{
	if (bIsPlaying)
	{
		StopPlayback();
	}
	else
	{
		UPaperFlipbook* FB = GetSelectedFlipbook();
		if (!FB || FB->GetNumKeyFrames() == 0) return;

		bIsPlaying = true;
		PreviousPlaybackFrame = INDEX_NONE;
		if (ActiveEditorEffects.IsValid()) ActiveEditorEffects->Empty();

		CachedTiming = FFlipbookTimingData::ReadFromFlipbook(FB);
		PlaybackTime = CachedTiming.GetFrameStartTime(SelectedFrameIndex);

		PlaybackTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateSP(this, &SFrameEventEditor::OnPlaybackTick), 1.0f / 60.0f);
	}
}

void SFrameEventEditor::StopPlayback()
{
	if (bIsPlaying)
	{
		bIsPlaying = false;
		if (PlaybackTickerHandle.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(PlaybackTickerHandle);
			PlaybackTickerHandle.Reset();
		}
		if (ActiveEditorEffects.IsValid()) ActiveEditorEffects->Empty();
		PreviousPlaybackFrame = INDEX_NONE;
	}
}

bool SFrameEventEditor::OnPlaybackTick(float DeltaTime)
{
	if (!bIsPlaying) return false;

	const float ActualDelta = FApp::GetDeltaTime();
	PlaybackTime += ActualDelta;

	// Loop wrap detection — clear all active effects when playback loops
	if (CachedTiming.TotalDurationSeconds > 0.0f && PlaybackTime >= CachedTiming.TotalDurationSeconds)
	{
		PlaybackTime = FMath::Fmod(PlaybackTime, CachedTiming.TotalDurationSeconds);
		if (ActiveEditorEffects.IsValid()) ActiveEditorEffects->Empty();
		PreviousPlaybackFrame = INDEX_NONE;
	}

	int32 NewFrame = GetFrameFromTime();
	if (NewFrame != SelectedFrameIndex)
	{
		SelectedFrameIndex = NewFrame;
		if (FrameStripBox.IsValid()) FrameStripBox->Invalidate(EInvalidateWidgetReason::Paint);
		if (TimelineTrack.IsValid()) TimelineTrack->Invalidate(EInvalidateWidgetReason::Paint);
	}

	// Detect frame transition for editor preview dispatch
	if (NewFrame != PreviousPlaybackFrame && PreviousPlaybackFrame != INDEX_NONE)
	{
		TArray<TObjectPtr<UPaper2DPlusFrameEventBase>>* Events = GetFrameEventsArray();
		if (Events)
		{
			for (UPaper2DPlusFrameEventBase* Event : *Events)
			{
				if (!Event) continue;

				// One-shot: fire when crossing trigger frame
				if (UPaper2DPlusFrameEvent* OneShot = Cast<UPaper2DPlusFrameEvent>(Event))
				{
					if (OneShot->TriggerFrame == NewFrame)
					{
						DispatchEditorPreview(OneShot);
					}
				}
			}
		}
	}
	PreviousPlaybackFrame = NewFrame;

	// Expire finished effects
	if (ActiveEditorEffects.IsValid())
	{
		ActiveEditorEffects->RemoveAll([this](const FEditorEffectPreview& P)
		{
			return P.Duration > 0.0f && (PlaybackTime - P.StartTime) >= P.Duration;
		});
	}

	Invalidate(EInvalidateWidgetReason::Paint);
	return true;
}

int32 SFrameEventEditor::GetFrameFromTime() const
{
	if (CachedTiming.TotalDurationSeconds <= 0.0f) return 0;
	return FrameEventEditorInternal::TimeToFrameIndex(CachedTiming, PlaybackTime);
}

void SFrameEventEditor::DispatchEditorPreview(UPaper2DPlusFrameEvent* Event)
{
	if (!Event) return;

	// For SpawnEffectFrameEvent: add visual preview to canvas
	if (UPaper2DPlusSpawnEffectFrameEvent* SpawnEffect = Cast<UPaper2DPlusSpawnEffectFrameEvent>(Event))
	{
		if (SpawnEffect->EffectFlipbook && ActiveEditorEffects.IsValid())
		{
			FEditorEffectPreview Preview;
			Preview.SourceEvent = SpawnEffect;
			Preview.StartTime = PlaybackTime;
			Preview.Duration = SpawnEffect->EffectFlipbook->GetTotalDuration();
			ActiveEditorEffects->Add(Preview);
		}
	}
}

// ─── FRAME SELECTION ─────────────────────────────────────────────────────────

void SFrameEventEditor::OnFrameClicked(int32 FrameIndex)
{
	if (FrameIndex == SelectedFrameIndex) return;
	StopPlayback();
	SelectedFrameIndex = FrameIndex;
	if (FrameStripBox.IsValid()) FrameStripBox->Invalidate(EInvalidateWidgetReason::Paint);
	if (TimelineTrack.IsValid()) TimelineTrack->Invalidate(EInvalidateWidgetReason::Paint);
	Invalidate(EInvalidateWidgetReason::Paint);
}

// ─── EVENT MANAGEMENT ────────────────────────────────────────────────────────

void SFrameEventEditor::ShowAddEventPicker()
{
	FMenuBuilder MenuBuilder(true, nullptr);

	// Collect available concrete subclasses (C++ and Blueprint) for each base type
	TArray<UClass*> OneShotClasses;
	TArray<UClass*> StateClasses;

	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* TestClass = *It;
		if (!TestClass || TestClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_Hidden))
		{
			continue;
		}

		if (TestClass->IsChildOf(UPaper2DPlusFrameEventState::StaticClass()))
		{
			StateClasses.Add(TestClass);
		}
		else if (TestClass->IsChildOf(UPaper2DPlusFrameEvent::StaticClass()))
		{
			OneShotClasses.Add(TestClass);
		}
	}

	// Sort by display name
	Algo::SortBy(OneShotClasses, [](UClass* C) { return C->GetDisplayNameText().ToString(); });
	Algo::SortBy(StateClasses, [](UClass* C) { return C->GetDisplayNameText().ToString(); });

	// One-Shot Events section
	MenuBuilder.BeginSection("OneShotEvents", LOCTEXT("OneShotSection", "One-Shot Events"));
	{
		for (UClass* EventClass : OneShotClasses)
		{
			const FText Label = EventClass->GetDisplayNameText();
			const bool bIsBP = EventClass->ClassGeneratedBy != nullptr;
			const FText Tooltip = bIsBP
				? FText::Format(LOCTEXT("BPEventTooltip", "Blueprint event: {0}"), FText::FromString(EventClass->GetPathName()))
				: FText::Format(LOCTEXT("NativeEventTooltip", "One-shot event that fires once at the trigger frame. Type: {0}"), Label);

			MenuBuilder.AddMenuEntry(
				Label, Tooltip, FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([this, EventClass]() { AddFrameEvent(EventClass); })));
		}

		if (OneShotClasses.Num() == 0)
		{
			MenuBuilder.AddMenuEntry(
				LOCTEXT("NoOneShotClasses", "(no one-shot event classes)"),
				FText::GetEmpty(), FSlateIcon(),
				FUIAction(), NAME_None, EUserInterfaceActionType::None);
		}

		MenuBuilder.AddMenuSeparator();
		MenuBuilder.AddMenuEntry(
			LOCTEXT("CreateNewOneShot", "Create New One-Shot Event..."),
			LOCTEXT("CreateNewOneShotTooltip", "Create a new Blueprint subclass of Frame Event for custom one-shot behavior"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Plus"),
			FUIAction(FExecuteAction::CreateLambda([this]() { CreateNewEventBlueprint(UPaper2DPlusFrameEvent::StaticClass()); })));
	}
	MenuBuilder.EndSection();

	// State Events section
	MenuBuilder.BeginSection("StateEvents", LOCTEXT("StateSection", "State Events (Begin/Tick/End)"));
	{
		for (UClass* EventClass : StateClasses)
		{
			const FText Label = EventClass->GetDisplayNameText();
			const bool bIsBP = EventClass->ClassGeneratedBy != nullptr;
			const FText Tooltip = bIsBP
				? FText::Format(LOCTEXT("BPStateTooltip", "Blueprint state event: {0}"), FText::FromString(EventClass->GetPathName()))
				: FText::Format(LOCTEXT("NativeStateTooltip", "Ranged state event with Begin/Tick/End lifecycle. Type: {0}"), Label);

			MenuBuilder.AddMenuEntry(
				Label, Tooltip, FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([this, EventClass]() { AddFrameEvent(EventClass); })));
		}

		if (StateClasses.Num() == 0)
		{
			MenuBuilder.AddMenuEntry(
				LOCTEXT("NoStateClasses", "(no state event classes)"),
				FText::GetEmpty(), FSlateIcon(),
				FUIAction(), NAME_None, EUserInterfaceActionType::None);
		}

		MenuBuilder.AddMenuSeparator();
		MenuBuilder.AddMenuEntry(
			LOCTEXT("CreateNewState", "Create New State Event..."),
			LOCTEXT("CreateNewStateTooltip", "Create a new Blueprint subclass of Frame Event State for custom ranged behavior (Begin/Tick/End)"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Plus"),
			FUIAction(FExecuteAction::CreateLambda([this]() { CreateNewEventBlueprint(UPaper2DPlusFrameEventState::StaticClass()); })));
	}
	MenuBuilder.EndSection();

	FSlateApplication::Get().PushMenu(
		SharedThis(this),
		FWidgetPath(),
		MenuBuilder.MakeWidget(),
		FSlateApplication::Get().GetCursorPos(),
		FPopupTransitionEffect::ContextMenu);
}

void SFrameEventEditor::CreateNewEventBlueprint(UClass* ParentClass)
{
	if (!ParentClass) return;

	// Determine default name based on parent
	const FString BaseName = ParentClass == UPaper2DPlusFrameEventState::StaticClass()
		? TEXT("NewFrameEventState")
		: TEXT("NewFrameEvent");

	// Use the asset tools to create a new Blueprint
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	FString DefaultPath = TEXT("/Game/Blueprints/FrameEvents");
	FString UniqueName;
	FString PackagePath;
	AssetTools.CreateUniqueAssetName(DefaultPath / BaseName, TEXT(""), PackagePath, UniqueName);

	// Open a save dialog so the user can choose the name and location
	UBlueprintFactory* Factory = NewObject<UBlueprintFactory>();
	Factory->ParentClass = ParentClass;

	UObject* NewAsset = AssetTools.CreateAssetWithDialog(UniqueName, DefaultPath, UBlueprint::StaticClass(), Factory);
	if (UBlueprint* NewBP = Cast<UBlueprint>(NewAsset))
	{
		// Open the BP editor so the user can add their logic
		GEditor->EditObject(NewBP);

		// Add an instance of the new BP class to the current flipbook
		if (NewBP->GeneratedClass)
		{
			AddFrameEvent(NewBP->GeneratedClass);
		}
	}
}

void SFrameEventEditor::AddFrameEvent(UClass* EventClass)
{
	if (!EventClass || !Asset.IsValid()) return;

	TArray<TObjectPtr<UPaper2DPlusFrameEventBase>>* Events = GetFrameEventsArray();
	if (!Events) return;

	BeginTransaction(LOCTEXT("AddFrameEventTxn", "Add Frame Event"));

	UPaper2DPlusFrameEventBase* NewEvent = NewObject<UPaper2DPlusFrameEventBase>(
		Asset.Get(), EventClass, NAME_None, RF_Transactional);
	Events->Add(NewEvent);

	EndTransaction();

	SelectedEventIndex = Events->Num() - 1;
	RefreshEventList();
	RefreshDetailsPanel();
	if (FrameStripBox.IsValid()) FrameStripBox->Invalidate(EInvalidateWidgetReason::Paint);
	if (TimelineTrack.IsValid()) TimelineTrack->Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
}

void SFrameEventEditor::RemoveEvent(int32 EventIndex)
{
	TArray<TObjectPtr<UPaper2DPlusFrameEventBase>>* Events = GetFrameEventsArray();
	if (!Events || !Events->IsValidIndex(EventIndex)) return;

	BeginTransaction(LOCTEXT("RemoveFrameEventTxn", "Remove Frame Event"));

	Events->RemoveAt(EventIndex);

	EndTransaction();

	SelectedEventIndex = INDEX_NONE;
	RefreshEventList();
	RefreshDetailsPanel();
	if (FrameStripBox.IsValid()) FrameStripBox->Invalidate(EInvalidateWidgetReason::Paint);
	if (TimelineTrack.IsValid()) TimelineTrack->Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
}

void SFrameEventEditor::OnEventSelected(int32 EventIndex)
{
	SelectedEventIndex = EventIndex;
	Invalidate(EInvalidateWidgetReason::Paint);
	if (TimelineTrack.IsValid()) TimelineTrack->Invalidate(EInvalidateWidgetReason::Paint);
	RefreshDetailsPanel();
}

// ─── KEYBOARD ────────────────────────────────────────────────────────────────

FReply SFrameEventEditor::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	// Guard: don't handle when text input is focused
	TSharedPtr<SWidget> Focused = FSlateApplication::Get().GetKeyboardFocusedWidget();
	if (Focused.IsValid())
	{
		FName Type = Focused->GetType();
		if (Type == TEXT("SEditableText") || Type == TEXT("SMultiLineEditableText"))
		{
			return FReply::Unhandled();
		}
	}

	const FKey Key = InKeyEvent.GetKey();

	// Guard Ctrl for shortcuts (Ctrl+S save, etc.)
	if (InKeyEvent.IsControlDown())
	{
		return FReply::Unhandled();
	}

	// Space: toggle playback
	if (Key == EKeys::SpaceBar)
	{
		TogglePlayback();
		return FReply::Handled();
	}

	// Delete/Backspace: remove selected event
	if (Key == EKeys::Delete || Key == EKeys::BackSpace)
	{
		if (SelectedEventIndex != INDEX_NONE)
		{
			RemoveEvent(SelectedEventIndex);
			return FReply::Handled();
		}
	}

	// Left/Right: frame navigation
	if (Key == EKeys::Left && GetFrameCount() > 0)
	{
		int32 NewFrame = FMath::Max(0, SelectedFrameIndex - 1);
		OnFrameClicked(NewFrame);
		return FReply::Handled();
	}
	if (Key == EKeys::Right && GetFrameCount() > 0)
	{
		int32 NewFrame = FMath::Min(GetFrameCount() - 1, SelectedFrameIndex + 1);
		OnFrameClicked(NewFrame);
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

// ─── HELPERS ─────────────────────────────────────────────────────────────────

FFlipbookProfileEntry* SFrameEventEditor::GetSelectedFlipbookData() const
{
	if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex)) return nullptr;
	return &Asset->Flipbooks[SelectedFlipbookIndex];
}

UPaperFlipbook* SFrameEventEditor::GetSelectedFlipbook() const
{
	FFlipbookProfileEntry* Data = GetSelectedFlipbookData();
	if (!Data || Data->Identity.Flipbook.IsNull()) return nullptr;
	return Data->Identity.Flipbook.LoadSynchronous();
}

int32 SFrameEventEditor::GetFrameCount() const
{
	UPaperFlipbook* FB = GetSelectedFlipbook();
	return FB ? FB->GetNumKeyFrames() : 0;
}

TArray<TObjectPtr<UPaper2DPlusFrameEventBase>>* SFrameEventEditor::GetFrameEventsArray() const
{
	FFlipbookProfileEntry* Data = GetSelectedFlipbookData();
	if (!Data) return nullptr;
	return &Data->FrameEventData.FrameEvents;
}

#undef LOCTEXT_NAMESPACE
