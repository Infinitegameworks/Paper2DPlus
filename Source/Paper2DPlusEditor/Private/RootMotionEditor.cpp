// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "RootMotionEditor.h"
#include "CharacterProfileEditorModel.h"
#include "FlipbookListBuilder.h"
#include "EditorCanvasUtils.h"
#include "ProfilePropertyRow.h"
#include "SlateShortcutUtils.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Input/SSearchBox.h"
#include "PaperFlipbook.h"
#include "PaperSprite.h"
#include "Framework/Application/SlateApplication.h"
#include "ScopedTransaction.h"

/** SRootMotionEditor — Root motion tab: per-frame position authoring with canvas visualization and trajectory preview. */

#define LOCTEXT_NAMESPACE "RootMotionEditor"

namespace RootMotionEditorInternal
{
/** Convert a playback time (seconds) to a frame index using timing data. */
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

const FName SRootMotionEditor::PositionPanelId(TEXT("Paper2DPlus.RootMotion.Position"));
const FName SRootMotionEditor::OnionSkinsPanelId(TEXT("Paper2DPlus.RootMotion.OnionSkins"));
const FName SRootMotionEditor::BatchPanelId(TEXT("Paper2DPlus.RootMotion.Batch"));

// ==========================================
// SRootMotionEditor — CONSTRUCT / DESTROY
// ==========================================

void SRootMotionEditor::Construct(const FArguments& InArgs)
{
	Asset = InArgs._Asset;
	Model = InArgs._Model;
	HostContract = InArgs._HostContract.IsValid()
		? InArgs._HostContract
		: FProfileToolPanelHostContract::Embedded();
	bHostActive = HostContract.OwnsEmbeddedNavigation();
	InitializeBatchOptions();

	if (GEditor) GEditor->RegisterForUndo(this);

	if (Model.IsValid())
	{
		SelectedFlipbookIndex = Model->GetSelectedFlipbookIndex();
		SelectedFrameIndex = Model->GetSelectedFrameIndex();

		ModelFlipbookSelectionHandle = Model->OnFlipbookSelectionChanged.AddSP(this, &SRootMotionEditor::OnModelFlipbookSelected);
		ModelFrameSelectionHandle = Model->OnFrameSelectionChanged.AddSP(
			this, &SRootMotionEditor::OnModelFrameSelected);
		ModelGroupCollapseHandle = Model->OnGroupCollapseChanged.AddLambda([this]() { });
		ModelSearchTextHandle = Model->OnSearchTextChanged.AddLambda([this](const FString&) { });
		ModelAssetExternallyModifiedHandle = Model->OnAssetExternallyModified.AddLambda([this]()
		{
			FinishActiveEditGesture(/*bReleaseCanvasCapture=*/true);
			RefreshAll();
		});
		// A frame reorder (and other in-editor data edits from sibling panels) changes
		// the RootMotion ordering WITHOUT changing the frame count, so OnFrameSelectionChanged
		// alone can no-op (its NewFrame == SelectedFrameIndex early-out). Subscribe to the
		// explicit data-changed signal so the motion canvas + frame strip repaint. Mirrors
		// SHitboxEditorPanel / SFrameTimingEditor.
		ModelAssetDataChangedHandle = Model->OnAssetDataChanged.AddLambda([this]()
		{
			if (bBroadcastingOwnDataChange) return;
			FinishActiveEditGesture(/*bReleaseCanvasCapture=*/true);
			RefreshAll();
		});
	}

	ChildSlot
	[
		HostContract.UsesExternalNavigation()
			? BuildCentralWorkspace()
			: StaticCastSharedRef<SWidget>(
				SNew(SSplitter)
				.Orientation(Orient_Horizontal)
				+ SSplitter::Slot()
				.Value(0.75f)
				[
					BuildCentralWorkspace()
				]
				+ SSplitter::Slot()
				.Value(0.25f)
				[
					SNew(SScrollBox)
					+ SScrollBox::Slot()
					[
						BuildEmbeddedContextStack()
					]
				])
	];

	// Wire canvas delegates
	MotionCanvas->OnDragStarted.BindLambda([this]()
	{
		BeginTransaction(LOCTEXT("MoveRootMotion", "Move Root Motion Position"));
		if (ActiveTransaction.IsValid())
		{
			EnsureRootMotionArraySized();
		}
	});
	MotionCanvas->OnDragEnded.BindLambda([this]()
	{
		EndTransaction();
		if (FrameStripBox.IsValid()) FrameStripBox->Invalidate(EInvalidateWidgetReason::Paint);
		if (PropertiesBox.IsValid()) PropertiesBox->Invalidate(EInvalidateWidgetReason::Paint);
	});
	MotionCanvas->OnPositionChanged.BindLambda([this](FVector2D NewPos)
	{
		SetCurrentFramePosition(NewPos);
	});

	RefreshAll();
}

SRootMotionEditor::~SRootMotionEditor()
{
	StopPlayback();
	FinishActiveEditGesture(/*bReleaseCanvasCapture=*/true);
	if (GEditor) GEditor->UnregisterForUndo(this);

	if (Model.IsValid())
	{
		Model->OnFlipbookSelectionChanged.Remove(ModelFlipbookSelectionHandle);
		Model->OnFrameSelectionChanged.Remove(ModelFrameSelectionHandle);
		Model->OnGroupCollapseChanged.Remove(ModelGroupCollapseHandle);
		Model->OnSearchTextChanged.Remove(ModelSearchTextHandle);
		Model->OnAssetExternallyModified.Remove(ModelAssetExternallyModifiedHandle);
		Model->OnAssetDataChanged.Remove(ModelAssetDataChangedHandle);
	}
}

void SRootMotionEditor::GetContextualPanels(
	TArray<FProfileToolPanelDescriptor>& OutPanels) const
{
	if (!HostContract.UsesExternalNavigation())
	{
		return;
	}

	const TWeakPtr<SRootMotionEditor> WeakController =
		ConstCastSharedRef<SRootMotionEditor>(SharedThis(this));
	auto AddPanel = [&OutPanels, WeakController](
		FName PanelId,
		const FText& Label,
		const FText& ToolTip,
		TFunction<TSharedRef<SWidget>(SRootMotionEditor&)> Builder)
	{
		FProfileToolPanelDescriptor Descriptor;
		Descriptor.PanelId = PanelId;
		Descriptor.Label = Label;
		Descriptor.ToolTip = ToolTip;
		Descriptor.CapabilityId = PanelId;
		Descriptor.IsAvailable = [WeakController]() { return WeakController.IsValid(); };
		Descriptor.WidgetFactory =
			[WeakController, PanelId, Builder = MoveTemp(Builder)]() -> TSharedRef<SWidget>
		{
			const TSharedPtr<SRootMotionEditor> Controller = WeakController.Pin();
			if (!Controller.IsValid())
			{
				return SNullWidget::NullWidget;
			}
			++Controller->ContextPanelBuildCounts.FindOrAdd(PanelId);
			Controller->ContextPanelResolvedFrames.FindOrAdd(PanelId) =
				Controller->Model.IsValid()
					? Controller->Model->GetSelectedFrameIndex()
					: Controller->SelectedFrameIndex;
			return Builder(*Controller);
		};
		OutPanels.Add(MoveTemp(Descriptor));
	};

	AddPanel(
		PositionPanelId,
		LOCTEXT("RootMotionPositionContext", "Position"),
		LOCTEXT("RootMotionPositionContextTip", "Edit or reset the live frame's root-motion position."),
		[](SRootMotionEditor& Controller) { return Controller.BuildPositionPanel(); });
	AddPanel(
		OnionSkinsPanelId,
		LOCTEXT("RootMotionOnionSkinsContext", "Onion & Skins"),
		LOCTEXT("RootMotionOnionSkinsContextTip", "Control backward and forward onion skins in the central motion preview."),
		[](SRootMotionEditor& Controller) { return Controller.BuildOnionSkinsPanel(); });
	AddPanel(
		BatchPanelId,
		LOCTEXT("RootMotionBatchContext", "Batch"),
		LOCTEXT("RootMotionBatchContextTip", "Set, offset, mirror, reset, or interpolate a chosen frame range."),
		[](SRootMotionEditor& Controller) { return Controller.BuildBatchPanel(); });
}

void SRootMotionEditor::HandleHostActivated()
{
	// The deferred seat's SetKeyboardFocus re-enters SDockTab activation synchronously. Suppress
	// only that re-entry so it cannot repeat the full refresh while focus is being committed.
	if (HostFocusSeat.IsApplying())
	{
		return;
	}
	bHostActive = true;
	RefreshAll();
	if (HostFocusSeat.ShouldRequestSeat(*this))
	{
		// Seat keyboard focus one paint after activation so the first Space press starts playback
		// without a preparatory click — the same deferred seat the Frame Cues tool uses.
		HostFocusSeat.TrackTimer(RegisterActiveTimer(
			0.0f,
			FWidgetActiveTimerDelegate::CreateSP(
				this,
				&SRootMotionEditor::ApplyDeferredHostFocus)));
	}
}

EActiveTimerReturnType SRootMotionEditor::ApplyDeferredHostFocus(
	double /*CurrentTime*/,
	float /*DeltaTime*/)
{
	return HostFocusSeat.ApplySeat(SharedThis(this), bHostActive);
}

void SRootMotionEditor::ApplyDeferredHostFocusForTests()
{
	if (const TSharedPtr<FActiveTimerHandle> Timer = HostFocusSeat.GetPendingTimer())
	{
		UnRegisterActiveTimer(Timer.ToSharedRef());
	}
	ApplyDeferredHostFocus(0.0, 0.0f);
}

void SRootMotionEditor::HandleHostDeactivated()
{
	StopPlayback();
	FinishActiveEditGesture(/*bReleaseCanvasCapture=*/true);
	bHostActive = false;
}

TSharedRef<SWidget> SRootMotionEditor::BuildCentralWorkspace()
{
	return SNew(SVerticalBox)
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
			.Text_Lambda([this]()
			{
				if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex))
				{
					return FText::FromString(TEXT("No Flipbook"));
				}
				const FFlipbookProfileEntry& Entry = Asset->Flipbooks[SelectedFlipbookIndex];
				UPaperFlipbook* Flipbook = Entry.Identity.Flipbook.Get();
				const int32 FrameCount = Flipbook ? Flipbook->GetNumKeyFrames() : 0;
				return FText::Format(
					LOCTEXT("MotionFlipbookTitleFmt", "{0}  Frame {1}/{2}"),
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
			SAssignNew(MotionCanvas, SRootMotionCanvas)
			.Asset(Asset.Get())
			.SelectedFlipbookIndex_Lambda([this]() { return SelectedFlipbookIndex; })
			.SelectedFrameIndex_Lambda([this]() { return SelectedFrameIndex; })
			.IsPlaying_Lambda([this]() { return bIsPlaying; })
			.PlaybackTime_Lambda([this]() { return PlaybackTime; })
			.ShowOnionSkin_Lambda([this]() { return bShowOnionSkin; })
			.ShowForwardOnionSkin_Lambda([this]() { return bShowForwardOnionSkin; })
			.OnionSkinFrames_Lambda([this]() { return OnionSkinFrames; })
			.OnionSkinOpacity_Lambda([this]() { return OnionSkinOpacity; })
			.PreviousFlipbookIndex_Lambda([this]()
			{
				return Model.IsValid() ? Model->GetQueueAdjacentFlipbookIndex(-1) : INDEX_NONE;
			})
			.NextFlipbookIndex_Lambda([this]()
			{
				return Model.IsValid() ? Model->GetQueueAdjacentFlipbookIndex(1) : INDEX_NONE;
			})
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.MaxHeight(80.0f)
		[
			SNew(SScrollBox)
			.Orientation(Orient_Horizontal)
			+ SScrollBox::Slot()
			[
				SAssignNew(FrameStripBox, SHorizontalBox)
			]
		];
}

TSharedRef<SWidget> SRootMotionEditor::BuildEmbeddedContextStack()
{
	return SAssignNew(PropertiesBox, SVerticalBox);
}

void SRootMotionEditor::SetCurrentFrameXForTests(float NewX)
{
	SetCurrentFrameX(NewX);
	CommitCurrentFramePositionEdit();
}

void SRootMotionEditor::SetCurrentFrameYForTests(float NewY)
{
	SetCurrentFrameY(NewY);
	CommitCurrentFramePositionEdit();
}

void SRootMotionEditor::BeginCanvasDragForTests()
{
	if (MotionCanvas.IsValid())
	{
		MotionCanvas->OnDragStarted.ExecuteIfBound();
	}
}

void SRootMotionEditor::DragCanvasToForTests(FVector2D NewPosition)
{
	if (MotionCanvas.IsValid())
	{
		MotionCanvas->OnPositionChanged.ExecuteIfBound(NewPosition);
	}
}

void SRootMotionEditor::EndCanvasDragForTests()
{
	if (MotionCanvas.IsValid())
	{
		MotionCanvas->OnDragEnded.ExecuteIfBound();
	}
}

void SRootMotionEditor::SetPathSkinStateForTests(
	bool bOnion,
	bool bForward,
	int32 Frames,
	float Opacity)
{
	bShowOnionSkin = bOnion;
	bShowForwardOnionSkin = bForward;
	OnionSkinFrames = FMath::Clamp(Frames, 1, 3);
	OnionSkinOpacity = FMath::Clamp(Opacity, 0.1f, 0.8f);
	if (MotionCanvas.IsValid())
	{
		MotionCanvas->Invalidate(EInvalidateWidgetReason::Paint);
	}
}

bool SRootMotionEditor::IsCanvasShowingOnionForTests() const
{
	return MotionCanvas.IsValid() && MotionCanvas->IsShowingOnionSkinForTests();
}

bool SRootMotionEditor::IsCanvasShowingForwardOnionForTests() const
{
	return MotionCanvas.IsValid() && MotionCanvas->IsShowingForwardOnionSkinForTests();
}

void SRootMotionEditor::ConfigureBatchForTests(
	int32 OperationIndex,
	int32 TargetIndex,
	FVector2D CustomValue,
	int32 RangeStart,
	int32 RangeEnd)
{
	MotionBatchSourceIndex = OperationIndex;
	MotionBatchTargetIndex = TargetIndex;
	MotionBatchCustomValue = CustomValue;
	MotionBatchRangeStart = RangeStart;
	MotionBatchRangeEnd = RangeEnd;
}

// ==========================================
// UNDO / REDO
// ==========================================

void SRootMotionEditor::PostUndo(bool bSuccess)
{
	StopPlayback();
	FinishActiveEditGesture(/*bReleaseCanvasCapture=*/true);
	bNeedsRefresh = true;
	if (bSuccess && (HostContract.OwnsEmbeddedNavigation() || bHostActive))
	{
		// Rebuild the frame strip, properties panel, and canvas so undo/redo isn't a visual no-op.
		// RefreshAll() clears bNeedsRefresh. Mirrors SFrameEventEditor::PostUndo (F1/U3).
		RefreshAll();
	}
}

void SRootMotionEditor::PostRedo(bool bSuccess) { PostUndo(bSuccess); }

// ==========================================
// TRANSACTION HELPERS
// ==========================================

void SRootMotionEditor::BeginTransaction(const FText& Description)
{
	if (ActiveTransaction.IsValid() || !CanMutateLiveSelection())
	{
		return;
	}
	UPaper2DPlusCharacterProfileAsset* Profile = Model.IsValid() ? Model->GetAsset() : Asset.Get();
	const int32 LiveFlipbookIndex = Model.IsValid()
		? Model->GetSelectedFlipbookIndex()
		: SelectedFlipbookIndex;
	const int32 LiveFrameIndex = Model.IsValid()
		? Model->GetSelectedFrameIndex()
		: SelectedFrameIndex;
	ActiveEditAnimation = Paper2DPlusProfileToolProvider::MakeAnimationIdentity(
		Profile, LiveFlipbookIndex);
	if (!ActiveEditAnimation.IsValid() || LiveFrameIndex < 0)
	{
		ActiveEditAnimation = {};
		return;
	}

	ActiveTransaction = MakeUnique<FScopedTransaction>(Description);
	ActiveEditFrameIndex = LiveFrameIndex;
	bActiveTransactionChanged = false;
	if (Profile)
	{
		Profile->Modify();
	}
	++TransactionBeginCount;
}

void SRootMotionEditor::EndTransaction()
{
	if (!ActiveTransaction.IsValid())
	{
		return;
	}
	const bool bChanged = bActiveTransactionChanged;
	UPaper2DPlusCharacterProfileAsset* Profile = Model.IsValid() ? Model->GetAsset() : Asset.Get();
	if (bChanged && Profile)
	{
		Profile->MarkPackageDirty();
	}
	ActiveTransaction.Reset();
	ActiveEditAnimation = {};
	ActiveEditFrameIndex = INDEX_NONE;
	bActiveTransactionChanged = false;
	++TransactionEndCount;
	if (bChanged)
	{
		NotifyMotionDataChanged();
	}
}

void SRootMotionEditor::FinishActiveEditGesture(bool bReleaseCanvasCapture)
{
	if (bReleaseCanvasCapture
		&& FSlateApplication::IsInitialized()
		&& MotionCanvas.IsValid()
		&& MotionCanvas->HasMouseCapture())
	{
		// Capture loss invokes OnDragEnded. EndTransaction below is the idempotent fallback.
		FSlateApplication::Get().ReleaseAllPointerCapture();
	}
	if (TSharedPtr<FActiveTimerHandle> Timer = NudgeDebounceTimer.Pin())
	{
		UnRegisterActiveTimer(Timer.ToSharedRef());
	}
	NudgeDebounceTimer.Reset();
	EndTransaction();
}

bool SRootMotionEditor::CanMutateLiveSelection() const
{
	return HostContract.OwnsEmbeddedNavigation() || bHostActive;
}

bool SRootMotionEditor::DoesActiveEditTargetLiveSelection() const
{
	if (!ActiveTransaction.IsValid() || !CanMutateLiveSelection())
	{
		return false;
	}
	const UPaper2DPlusCharacterProfileAsset* Profile =
		Model.IsValid() ? Model->GetAsset() : Asset.Get();
	const int32 LiveFlipbookIndex = Model.IsValid()
		? Model->GetSelectedFlipbookIndex()
		: SelectedFlipbookIndex;
	const int32 LiveFrameIndex = Model.IsValid()
		? Model->GetSelectedFrameIndex()
		: SelectedFrameIndex;
	return Paper2DPlusProfileToolProvider::ResolveAnimationIndex(
			Profile, ActiveEditAnimation) == LiveFlipbookIndex
		&& ActiveEditFrameIndex == LiveFrameIndex;
}

void SRootMotionEditor::MarkActiveTransactionChanged()
{
	if (ActiveTransaction.IsValid())
	{
		bActiveTransactionChanged = true;
	}
}

void SRootMotionEditor::NotifyMotionDataChanged()
{
	if (!Model.IsValid())
	{
		return;
	}
	bBroadcastingOwnDataChange = true;
	Model->NotifyAssetDataChanged();
	bBroadcastingOwnDataChange = false;
}

// ==========================================
// TOOLBAR
// ==========================================

TSharedRef<SWidget> SRootMotionEditor::BuildToolbar()
{
	// Empty toolbar — skins mixer moved to properties panel
	return SNullWidget::NullWidget;
}

// ==========================================
// FLIPBOOK LIST
// ==========================================

TSharedRef<SWidget> SRootMotionEditor::BuildFlipbookList()
{
	return SNullWidget::NullWidget; // Rebuilt in RefreshFlipbookList
}

void SRootMotionEditor::RefreshFlipbookList()
{
	if (!FlipbookListBox.IsValid() || !Asset.IsValid()) return;
	FlipbookListBox->ClearChildren();

	const FString& SearchFilter = Model.IsValid() ? Model->GetFlipbookGroupSearchText() : FString();

	auto ItemBuilder = [this](int32 i) -> TSharedRef<SWidget>
	{
		const FFlipbookProfileEntry& Anim = Asset->Flipbooks[i];
		UPaperFlipbook* LoadedFlipbook = !Anim.Identity.Flipbook.IsNull() ? Anim.Identity.Flipbook.LoadSynchronous() : nullptr;
		const bool bHasMotion = Anim.MotionData.HasRootMotion();

		return SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "NoBorder")
			.OnClicked_Lambda([this, i]()
			{
				if (Model.IsValid()) Model->SetSelectedFlipbook(i);
				return FReply::Handled();
			})
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
				.BorderBackgroundColor_Lambda([this, i]()
				{
					return (i == SelectedFlipbookIndex)
						? FLinearColor(0.15f, 0.35f, 0.55f, 1.0f)
						: FLinearColor(0.03f, 0.03f, 0.03f, 1.0f);
				})
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
							LoadedFlipbook
								? StaticCastSharedRef<SWidget>(SNew(SFlipbookThumbnail).Flipbook(LoadedFlipbook))
								: StaticCastSharedRef<SWidget>(SNew(SBorder)
									.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.DarkGroupBorder"))
									.HAlign(HAlign_Center).VAlign(VAlign_Center)
									[
										SNew(STextBlock)
										.Text(LOCTEXT("NoFBList", "No FB"))
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
							SNew(STextBlock)
							.Text(FText::FromString(Anim.Identity.FlipbookName))
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(STextBlock)
							.Visibility(bHasMotion ? EVisibility::Visible : EVisibility::Collapsed)
							.Text_Lambda([this, i]() -> FText {
								if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(i)) return FText::GetEmpty();
								const FFlipbookMotionData& Motion = Asset->Flipbooks[i].MotionData;
								if (!Motion.HasRootMotion()) return FText::GetEmpty();
								for (int32 f = Motion.RootMotion.Num() - 1; f >= 0; --f)
								{
									if (!Motion.RootMotion[f].Position.IsNearlyZero())
									{
										return FText::Format(LOCTEXT("RMPosBadge", "({0}, {1})"),
											FText::AsNumber(FMath::RoundToInt(Motion.RootMotion[f].Position.X)),
											FText::AsNumber(FMath::RoundToInt(Motion.RootMotion[f].Position.Y)));
									}
								}
								return FText::GetEmpty();
							})
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
							.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.8f, 0.5f)))
						]
					]
				]
			];
	};

	TFunction<bool(int32)> FilterFn = nullptr;
	if (!SearchFilter.IsEmpty())
	{
		FilterFn = [this, &SearchFilter](int32 Idx) -> bool
		{
			return Asset->Flipbooks[Idx].Identity.FlipbookName.Contains(SearchFilter, ESearchCase::IgnoreCase);
		};
	}

	FFlipbookListBuilder::Build(FlipbookListBox, Model, ItemBuilder, [this]() { RefreshFlipbookList(); }, FilterFn);
}

// ==========================================
// FRAME STRIP
// ==========================================

TSharedRef<SWidget> SRootMotionEditor::BuildFrameStrip()
{
	return SNullWidget::NullWidget; // Rebuilt in RefreshFrameStrip
}

void SRootMotionEditor::RefreshFrameStrip()
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

		// Color bar = green if frame has authored root motion data
		CellArgs.bShowBottomBar = true;
		CellArgs.BottomBarColor = TAttribute<FLinearColor>::CreateLambda([this, i]()
		{
			FFlipbookProfileEntry* Data = GetSelectedFlipbookData();
			bool bHasMotion = Data && Data->MotionData.RootMotion.IsValidIndex(i) &&
				!Data->MotionData.RootMotion[i].Position.IsNearlyZero();
			return bHasMotion ? FLinearColor(0.3f, 0.8f, 0.3f) : FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);
		});

		FrameStripBox->AddSlot()
		.AutoWidth()
		.Padding(1, 0)
		[
			FFrameStripCellUtils::Build(CellArgs)
		];
	}
}

// ==========================================
// PROPERTIES PANEL
// ==========================================

void SRootMotionEditor::RefreshPropertiesPanel()
{
	if (!PropertiesBox.IsValid())
	{
		return;
	}
	PropertiesBox->ClearChildren();
	if (!GetSelectedFlipbookData())
	{
		return;
	}
	PropertiesBox->AddSlot()
	.AutoHeight()
	[
		BuildPropertiesPanel()
	];
}

TSharedRef<SWidget> SRootMotionEditor::BuildPropertiesPanel()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			BuildOnionSkinsPanel()
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(8.0f, 2.0f)
		[
			SNew(SSeparator)
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			BuildPositionPanel()
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(8.0f, 4.0f)
		[
			SNew(SSeparator)
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			BuildBatchPanel()
		];
}

TSharedRef<SWidget> SRootMotionEditor::BuildOnionSkinsPanel()
{
	// Shared with the Sprite tool: one vertical stack of channel rows on the common property grid.
	auto Repaint = [this]()
	{
		if (MotionCanvas.IsValid()) MotionCanvas->Invalidate(EInvalidateWidgetReason::Paint);
	};

	FProfileOnionChannelArgs Onion;
	Onion.Label = LOCTEXT("OnionLabel", "Onion");
	Onion.LabelColor = FLinearColor(0.5f, 0.5f, 1.0f);
	Onion.Tooltip = LOCTEXT("OnionTip", "Show previous frames behind the current frame.");
	Onion.IsChecked = TAttribute<ECheckBoxState>::CreateLambda([this]()
		{ return bShowOnionSkin ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; });
	Onion.OnCheckStateChanged = [this, Repaint](ECheckBoxState State)
		{ bShowOnionSkin = State == ECheckBoxState::Checked; Repaint(); };
	Onion.Opacity = TAttribute<float>::CreateLambda([this]() { return OnionSkinOpacity; });
	Onion.OnOpacityChanged = [this, Repaint](float Value) { OnionSkinOpacity = Value; Repaint(); };
	Onion.FrameCount = TAttribute<int32>::CreateLambda([this]() { return OnionSkinFrames; });
	Onion.OnFrameCountChanged = [this, Repaint](int32 Value) { OnionSkinFrames = Value; Repaint(); };

	FProfileOnionChannelArgs Forward;
	Forward.Label = LOCTEXT("ForwardLabel", "Forward");
	Forward.LabelColor = FLinearColor(0.5f, 1.0f, 0.5f);
	Forward.Tooltip = LOCTEXT("ForwardTip", "Show following frames ahead of the current frame.");
	Forward.IsChecked = TAttribute<ECheckBoxState>::CreateLambda([this]()
		{ return bShowForwardOnionSkin ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; });
	Forward.OnCheckStateChanged = [this, Repaint](ECheckBoxState State)
		{ bShowForwardOnionSkin = State == ECheckBoxState::Checked; Repaint(); };
	Forward.Opacity = TAttribute<float>::CreateLambda([this]() { return OnionSkinOpacity; });
	Forward.OnOpacityChanged = [this, Repaint](float Value) { OnionSkinOpacity = Value; Repaint(); };
	Forward.FrameCount = TAttribute<int32>::CreateLambda([this]() { return OnionSkinFrames; });
	Forward.OnFrameCountChanged = [this, Repaint](int32 Value) { OnionSkinFrames = Value; Repaint(); };

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			FProfilePropertyRowUtils::MakeSectionHint(LOCTEXT(
				"MotionPathAlwaysVisible",
				"The authored motion path remains visible in the central preview."))
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			FProfilePropertyRowUtils::MakeOnionChannelRow(Onion)
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			FProfilePropertyRowUtils::MakeOnionChannelRow(Forward)
		];
}

TSharedRef<SWidget> SRootMotionEditor::BuildPositionPanel()
{
	auto BeginPositionEdit = [this](const FText& Description)
	{
		BeginTransaction(Description);
		if (ActiveTransaction.IsValid())
		{
			EnsureRootMotionArraySized();
		}
	};

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(8, 4, 8, 2)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("PositionHeader", "Position"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(8, 4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("XLabel", "X"))
				.ToolTipText(LOCTEXT(
					"RootMotionXTip",
					"Horizontal root motion position for this frame in pixels. Runtime movement uses deltas between successive positions."))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.8f, 0.3f, 0.3f)))
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SNew(SSpinBox<float>)
				.MinValue(-10000.0f).MaxValue(10000.0f).Delta(1.0f)
				.Value_Lambda([this]() { return GetCurrentFramePosition().X; })
				.OnValueChanged_Lambda([this](float Value) { SetCurrentFrameX(Value); })
				.OnValueCommitted_Lambda([this](float, ETextCommit::Type)
				{
					CommitCurrentFramePositionEdit();
				})
				.OnBeginSliderMovement_Lambda([BeginPositionEdit]()
				{
					BeginPositionEdit(LOCTEXT("EditRootMotionX", "Edit Root Motion X"));
				})
				.OnEndSliderMovement_Lambda([this](float)
				{
					CommitCurrentFramePositionEdit();
				})
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(8, 4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("YLabel", "Y"))
				.ToolTipText(LOCTEXT(
					"RootMotionYTip",
					"Vertical root motion position for this frame in pixels. Positive values move down in Paper2D coordinates."))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.3f, 0.8f, 0.3f)))
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SNew(SSpinBox<float>)
				.MinValue(-10000.0f).MaxValue(10000.0f).Delta(1.0f)
				.Value_Lambda([this]() { return GetCurrentFramePosition().Y; })
				.OnValueChanged_Lambda([this](float Value) { SetCurrentFrameY(Value); })
				.OnValueCommitted_Lambda([this](float, ETextCommit::Type)
				{
					CommitCurrentFramePositionEdit();
				})
				.OnBeginSliderMovement_Lambda([BeginPositionEdit]()
				{
					BeginPositionEdit(LOCTEXT("EditRootMotionY", "Edit Root Motion Y"));
				})
				.OnEndSliderMovement_Lambda([this](float)
				{
					CommitCurrentFramePositionEdit();
				})
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(8, 4)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.Text(LOCTEXT("ResetCurrentPosition", "Reset Current Frame"))
			.ToolTipText(LOCTEXT(
				"ResetCurrentPositionTip",
				"Set the live frame's root-motion position to (0, 0)."))
			.IsEnabled_Lambda([this]()
			{
				return CanMutateLiveSelection() && !GetCurrentFramePosition().IsNearlyZero();
			})
			.OnClicked_Lambda([this]()
			{
				ResetCurrentFrame();
				return FReply::Handled();
			})
		];
}

void SRootMotionEditor::InitializeBatchOptions()
{
	MotionBatchOperationOptions.Reset();
	MotionBatchOperationOptions.Add(MakeShared<FString>(TEXT("Current Frame's Position")));
	MotionBatchOperationOptions.Add(MakeShared<FString>(TEXT("Reset (0, 0)")));
	MotionBatchOperationOptions.Add(MakeShared<FString>(TEXT("Custom Position")));
	MotionBatchOperationOptions.Add(MakeShared<FString>(TEXT("Interpolate")));
	// Backward compatible: U25 appends the two new operations after the established 0-3 meanings.
	MotionBatchOperationOptions.Add(MakeShared<FString>(TEXT("Offset by Custom")));
	MotionBatchOperationOptions.Add(MakeShared<FString>(TEXT("Mirror X")));

	MotionBatchTargetOptions.Reset();
	MotionBatchTargetOptions.Add(MakeShared<FString>(TEXT("All Frames")));
	MotionBatchTargetOptions.Add(MakeShared<FString>(TEXT("Selected Frame")));
	MotionBatchTargetOptions.Add(MakeShared<FString>(TEXT("Remaining Frames")));
	MotionBatchTargetOptions.Add(MakeShared<FString>(TEXT("Custom Range")));
}

TSharedRef<SWidget> SRootMotionEditor::BuildBatchPanel()
{
	// The old local combo clamped a stale index back to 0 before building; keep that normalization.
	MotionBatchSourceIndex = MotionBatchOperationOptions.IsValidIndex(MotionBatchSourceIndex) ? MotionBatchSourceIndex : 0;
	MotionBatchTargetIndex = MotionBatchTargetOptions.IsValidIndex(MotionBatchTargetIndex) ? MotionBatchTargetIndex : 0;

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(8, 0, 8, 4)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("BatchOpsHeader", "Batch Position Tools"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			FProfilePropertyRowUtils::MakeRow(
				LOCTEXT("ApplyOperationLabel", "Apply"),
				FProfilePropertyRowUtils::MakeStringCombo(&MotionBatchOperationOptions, &MotionBatchSourceIndex))
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox)
			.Visibility_Lambda([this]()
			{
				return MotionBatchSourceIndex == 2 || MotionBatchSourceIndex == 4
					? EVisibility::Visible
					: EVisibility::Collapsed;
			})
			[
				FProfilePropertyRowUtils::MakeRow(
					LOCTEXT("CustValueLabel", "Value"),
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 2, 0)
					[
						SNew(SSpinBox<float>)
						.MinValue(-9999.0f).MaxValue(9999.0f).Delta(1.0f)
						.ToolTipText(LOCTEXT("CustXTip", "Custom value X"))
						.Value_Lambda([this]() { return MotionBatchCustomValue.X; })
						.OnValueChanged_Lambda([this](float Value)
						{
							MotionBatchCustomValue.X = Value;
						})
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f)
					[
						SNew(SSpinBox<float>)
						.MinValue(-9999.0f).MaxValue(9999.0f).Delta(1.0f)
						.ToolTipText(LOCTEXT("CustYTip", "Custom value Y"))
						.Value_Lambda([this]() { return MotionBatchCustomValue.Y; })
						.OnValueChanged_Lambda([this](float Value)
						{
							MotionBatchCustomValue.Y = Value;
						})
					],
					FText::GetEmpty(), 0.0f, 0.0f)
			]
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			FProfilePropertyRowUtils::MakeRow(
				LOCTEXT("ToLabel", "to"),
				FProfilePropertyRowUtils::MakeStringCombo(&MotionBatchTargetOptions, &MotionBatchTargetIndex))
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox)
			.Visibility_Lambda([this]()
			{
				return MotionBatchTargetIndex == 3
					? EVisibility::Visible
					: EVisibility::Collapsed;
			})
			[
				FProfilePropertyRowUtils::MakeRow(
					LOCTEXT("BatchRangeLabel", "Range"),
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 4, 0)
					[
						SNew(SSpinBox<int32>)
						.MinValue(0)
						.ToolTipText(LOCTEXT("BatchRangeFromTip", "First frame of the range"))
						.MaxValue_Lambda([this]() { return FMath::Max(0, GetFrameCount() - 1); })
						.Value_Lambda([this]() { return MotionBatchRangeStart; })
						.OnValueChanged_Lambda([this](int32 Value) { MotionBatchRangeStart = Value; })
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
					[
						SNew(STextBlock).Text(LOCTEXT("BatchRangeJoin", "to")).Font(FProfilePropertyRowUtils::GetPropertyFont())
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f)
					[
						SNew(SSpinBox<int32>)
						.MinValue(0)
						.ToolTipText(LOCTEXT("BatchRangeToTip", "Last frame of the range"))
						.MaxValue_Lambda([this]() { return FMath::Max(0, GetFrameCount() - 1); })
						.Value_Lambda([this]() { return MotionBatchRangeEnd; })
						.OnValueChanged_Lambda([this](int32 Value) { MotionBatchRangeEnd = Value; })
					],
					FText::GetEmpty(), 0.0f, 0.0f)
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(8, 4)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.HAlign(HAlign_Center)
			.ToolTipText_Lambda([this]()
			{
				if (MotionBatchSourceIndex == 3 && MotionBatchTargetIndex != 3)
				{
					return LOCTEXT(
						"ApplyBatchMotionTipLerp",
						"Interpolate requires a Custom Range target.");
				}
				return LOCTEXT(
					"ApplyBatchMotionTip",
					"Apply the selected operation to the selected frame target.");
			})
			.IsEnabled_Lambda([this]()
			{
				const FFlipbookProfileEntry* Data = GetSelectedFlipbookData();
				if (!Data || !CanMutateLiveSelection()) return false;
				if (MotionBatchSourceIndex == 0
					&& !Data->MotionData.RootMotion.IsValidIndex(SelectedFrameIndex)) return false;
				if (MotionBatchSourceIndex == 3 && MotionBatchTargetIndex != 3) return false;
				if (MotionBatchSourceIndex == 5 && !Data->MotionData.HasRootMotion()) return false;
				return true;
			})
			.OnClicked_Lambda([this]()
			{
				OnApplyMotionBatchOperation();
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ApplyBatch", "Apply"))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			]
		];
}
// ==========================================
// FRAME SELECTION
// ==========================================

void SRootMotionEditor::OnFrameClicked(int32 FrameIndex)
{
	if (FrameIndex == SelectedFrameIndex) return;
	FinishActiveEditGesture(/*bReleaseCanvasCapture=*/true);
	if (Model.IsValid())
	{
		Model->SetSelectedFrame(FrameIndex);
	}
	else
	{
		SelectedFrameIndex = FrameIndex;
		if (FrameStripBox.IsValid()) FrameStripBox->Invalidate(EInvalidateWidgetReason::Paint);
		if (PropertiesBox.IsValid()) PropertiesBox->Invalidate(EInvalidateWidgetReason::Paint);
		if (MotionCanvas.IsValid()) MotionCanvas->Invalidate(EInvalidateWidgetReason::Paint);
	}
}

// ==========================================
// ROOT MOTION EDITING
// ==========================================

void SRootMotionEditor::EnsureRootMotionArraySized()
{
	FFlipbookProfileEntry* Data = GetSelectedFlipbookData();
	if (!Data) return;

	int32 FrameCount = GetFrameCount();
	if (FrameCount <= 0) return;

	if (Data->MotionData.RootMotion.Num() < FrameCount)
	{
		Data->MotionData.RootMotion.SetNum(FrameCount);
		MarkActiveTransactionChanged();
	}
}

bool SRootMotionEditor::SetCurrentFramePosition(FVector2D NewPosition)
{
	if (!DoesActiveEditTargetLiveSelection() || !Asset.IsValid()) return false;

	EnsureRootMotionArraySized();
	FFlipbookProfileEntry* Data = GetSelectedFlipbookData();
	if (!Data) return false;
	if (Data->MotionData.RootMotion.IsValidIndex(SelectedFrameIndex))
	{
		if (Data->MotionData.RootMotion[SelectedFrameIndex].Position.Equals(NewPosition))
		{
			return false;
		}
		Data->MotionData.RootMotion[SelectedFrameIndex].Position = NewPosition;
		MarkActiveTransactionChanged();
		if (MotionCanvas.IsValid()) MotionCanvas->Invalidate(EInvalidateWidgetReason::Paint);
		return true;
	}
	return false;
}

void SRootMotionEditor::SetCurrentFrameX(float NewX)
{
	if (!CanMutateLiveSelection()) return;
	if (!ActiveTransaction.IsValid())
	{
		BeginTransaction(LOCTEXT("EditRootMotionX", "Edit Root Motion X"));
	}
	if (!ActiveTransaction.IsValid()) return;
	EnsureRootMotionArraySized();
	FVector2D Position = GetCurrentFramePosition();
	Position.X = NewX;
	SetCurrentFramePosition(Position);
}

void SRootMotionEditor::SetCurrentFrameY(float NewY)
{
	if (!CanMutateLiveSelection()) return;
	if (!ActiveTransaction.IsValid())
	{
		BeginTransaction(LOCTEXT("EditRootMotionY", "Edit Root Motion Y"));
	}
	if (!ActiveTransaction.IsValid()) return;
	EnsureRootMotionArraySized();
	FVector2D Position = GetCurrentFramePosition();
	Position.Y = NewY;
	SetCurrentFramePosition(Position);
}

void SRootMotionEditor::CommitCurrentFramePositionEdit()
{
	FinishActiveEditGesture();
	if (FrameStripBox.IsValid()) FrameStripBox->Invalidate(EInvalidateWidgetReason::Paint);
	if (PropertiesBox.IsValid()) PropertiesBox->Invalidate(EInvalidateWidgetReason::Paint);
}

void SRootMotionEditor::OnApplyMotionBatchOperation()
{
	if (!CanMutateLiveSelection()) return;
	FFlipbookProfileEntry* Data = GetSelectedFlipbookData();
	if (!Data) return;

	// Do NOT grow the array before the transaction (F29). Validate/build targets against the flipbook's
	// key-frame count; the array is grown after each BeginTransaction below so undo can shrink it back.
	const int32 FrameCount = GetFrameCount();
	if (FrameCount == 0) return;

	// Reset-on-empty is a no-op: writing all-(0,0) into a fresh array would still flip HasRootMotion()
	// (RootMotion.Num() > 0) permanently true. With no authored motion there is nothing to reset.
	if (MotionBatchSourceIndex == 1 && !Data->MotionData.HasRootMotion()) return;
	if (MotionBatchSourceIndex == 4 && MotionBatchCustomValue.IsNearlyZero()) return;
	if (MotionBatchSourceIndex == 5 && !Data->MotionData.HasRootMotion()) return;

	// Determine source position
	FVector2D SourcePos = FVector2D::ZeroVector;
	if (MotionBatchSourceIndex == 0) // Current Frame's Position
	{
		if (!Data->MotionData.RootMotion.IsValidIndex(SelectedFrameIndex)) return;
		SourcePos = Data->MotionData.RootMotion[SelectedFrameIndex].Position;
	}
	else if (MotionBatchSourceIndex == 1) // Reset (0, 0)
	{
		SourcePos = FVector2D::ZeroVector;
	}
	else if (MotionBatchSourceIndex == 2) // Custom Position
	{
		SourcePos = MotionBatchCustomValue;
	}
	else if (MotionBatchSourceIndex == 3) // Interpolate
	{
		// Interpolate requires Custom Range target
		if (MotionBatchTargetIndex != 3) return;
		int32 First = FMath::Clamp(FMath::Min(MotionBatchRangeStart, MotionBatchRangeEnd), 0, FrameCount - 1);
		int32 Last = FMath::Clamp(FMath::Max(MotionBatchRangeStart, MotionBatchRangeEnd), 0, FrameCount - 1);
		if (First == Last || !Data->MotionData.RootMotion.IsValidIndex(First) || !Data->MotionData.RootMotion.IsValidIndex(Last)) return;

		const FVector2D StartPos = Data->MotionData.RootMotion[First].Position;
		const FVector2D EndPos = Data->MotionData.RootMotion[Last].Position;
		const int32 Range = Last - First;

		BeginTransaction(LOCTEXT("LerpRootMotion", "Interpolate Root Motion"));
		if (!ActiveTransaction.IsValid()) return;
		for (int32 i = First; i <= Last; i++)
		{
			const float Alpha = static_cast<float>(i - First) / static_cast<float>(Range);
			const FVector2D NewPosition = FMath::Lerp(StartPos, EndPos, Alpha);
			if (!Data->MotionData.RootMotion[i].Position.Equals(NewPosition))
			{
				Data->MotionData.RootMotion[i].Position = NewPosition;
				MarkActiveTransactionChanged();
			}
		}
		EndTransaction();

		if (FrameStripBox.IsValid()) FrameStripBox->Invalidate(EInvalidateWidgetReason::Paint);
		if (MotionCanvas.IsValid()) MotionCanvas->Invalidate(EInvalidateWidgetReason::Paint);
		if (PropertiesBox.IsValid()) PropertiesBox->Invalidate(EInvalidateWidgetReason::Paint);
		return; // Early return - don't fall through to the generic apply below
	}
	else if (MotionBatchSourceIndex != 4 && MotionBatchSourceIndex != 5)
	{
		return;
	}

	// Build target frame list
	TArray<int32> TargetFrames;
	if (MotionBatchTargetIndex == 0) // All Frames
	{
		for (int32 i = 0; i < FrameCount; i++) TargetFrames.Add(i);
	}
	else if (MotionBatchTargetIndex == 1) // Selected Frames (current frame only since root motion has no multi-select)
	{
		// Array may not be sized yet (grown after BeginTransaction below) — bound against FrameCount.
		if (SelectedFrameIndex >= 0 && SelectedFrameIndex < FrameCount)
		{
			TargetFrames.Add(SelectedFrameIndex);
		}
	}
	else if (MotionBatchTargetIndex == 2) // Remaining Frames
	{
		for (int32 i = SelectedFrameIndex; i < FrameCount; i++) TargetFrames.Add(i);
	}
	else if (MotionBatchTargetIndex == 3) // Custom Range
	{
		int32 First = FMath::Clamp(FMath::Min(MotionBatchRangeStart, MotionBatchRangeEnd), 0, FrameCount - 1);
		int32 Last = FMath::Clamp(FMath::Max(MotionBatchRangeStart, MotionBatchRangeEnd), 0, FrameCount - 1);
		for (int32 i = First; i <= Last; i++) TargetFrames.Add(i);
	}

	if (TargetFrames.Num() == 0) return;

	const FText TransactionLabel = MotionBatchSourceIndex == 4
		? LOCTEXT("BatchOffsetRootMotion", "Batch Offset Root Motion")
		: MotionBatchSourceIndex == 5
			? LOCTEXT("BatchMirrorRootMotion", "Batch Mirror Root Motion")
			: LOCTEXT("BatchSetRootMotion", "Batch Set Root Motion");
	BeginTransaction(TransactionLabel);
	if (!ActiveTransaction.IsValid()) return;
	if (MotionBatchSourceIndex != 5)
	{
		// Grow inside the transaction so undo can shrink an all-new array back (F29).
		EnsureRootMotionArraySized();
	}
	for (int32 Idx : TargetFrames)
	{
		if (Data->MotionData.RootMotion.IsValidIndex(Idx))
		{
			const FVector2D OldPosition = Data->MotionData.RootMotion[Idx].Position;
			FVector2D NewPosition = SourcePos;
			if (MotionBatchSourceIndex == 4)
			{
				NewPosition = OldPosition + MotionBatchCustomValue;
			}
			else if (MotionBatchSourceIndex == 5)
			{
				NewPosition = FVector2D(-OldPosition.X, OldPosition.Y);
			}
			if (!OldPosition.Equals(NewPosition))
			{
				Data->MotionData.RootMotion[Idx].Position = NewPosition;
				MarkActiveTransactionChanged();
			}
		}
	}
	EndTransaction();

	if (FrameStripBox.IsValid()) FrameStripBox->Invalidate(EInvalidateWidgetReason::Paint);
	if (MotionCanvas.IsValid()) MotionCanvas->Invalidate(EInvalidateWidgetReason::Paint);
	if (PropertiesBox.IsValid()) PropertiesBox->Invalidate(EInvalidateWidgetReason::Paint);
}

void SRootMotionEditor::ResetCurrentFrame()
{
	if (!CanMutateLiveSelection()) return;
	FFlipbookProfileEntry* Data = GetSelectedFlipbookData();
	if (!Data) return;

	if (!Data->MotionData.RootMotion.IsValidIndex(SelectedFrameIndex)) return;
	if (Data->MotionData.RootMotion[SelectedFrameIndex].Position.IsNearlyZero()) return;

	BeginTransaction(LOCTEXT("ResetRootMotionFrame", "Reset Root Motion Frame"));
	if (!ActiveTransaction.IsValid()) return;
	Data->MotionData.RootMotion[SelectedFrameIndex].Position = FVector2D::ZeroVector;
	MarkActiveTransactionChanged();
	EndTransaction();

	if (FrameStripBox.IsValid()) FrameStripBox->Invalidate(EInvalidateWidgetReason::Paint);
	if (PropertiesBox.IsValid()) PropertiesBox->Invalidate(EInvalidateWidgetReason::Paint);
	if (MotionCanvas.IsValid()) MotionCanvas->Invalidate(EInvalidateWidgetReason::Paint);
}

FVector2D SRootMotionEditor::GetCurrentFramePosition() const
{
	FFlipbookProfileEntry* Data = GetSelectedFlipbookData();
	if (!Data || !Data->MotionData.RootMotion.IsValidIndex(SelectedFrameIndex)) return FVector2D::ZeroVector;
	return Data->MotionData.RootMotion[SelectedFrameIndex].Position;
}

// ==========================================
// PLAYBACK
// ==========================================

void SRootMotionEditor::TogglePlayback()
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

		CachedTiming = FFlipbookTimingData::ReadFromFlipbook(FB);
		PlaybackTime = CachedTiming.GetFrameStartTime(SelectedFrameIndex);

		PlaybackTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateSP(this, &SRootMotionEditor::OnPlaybackTick), 1.0f / 60.0f);
	}
}

void SRootMotionEditor::StopPlayback()
{
	if (bIsPlaying)
	{
		bIsPlaying = false;
		if (PlaybackTickerHandle.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(PlaybackTickerHandle);
			PlaybackTickerHandle.Reset();
		}
	}
}

bool SRootMotionEditor::OnPlaybackTick(float DeltaTime)
{
	if (!bIsPlaying) return false;

	const float ActualDelta = FApp::GetDeltaTime();
	PlaybackTime += ActualDelta;

	// Loop
	if (CachedTiming.TotalDurationSeconds > 0.0f && PlaybackTime >= CachedTiming.TotalDurationSeconds)
	{
		PlaybackTime = FMath::Fmod(PlaybackTime, CachedTiming.TotalDurationSeconds);
	}

	int32 NewFrame = GetFrameFromTime();
	if (NewFrame != SelectedFrameIndex)
	{
		if (Model.IsValid())
		{
			Model->SetSelectedFrame(NewFrame);
		}
		else
		{
			SelectedFrameIndex = NewFrame;
			if (FrameStripBox.IsValid()) FrameStripBox->Invalidate(EInvalidateWidgetReason::Paint);
			if (PropertiesBox.IsValid()) PropertiesBox->Invalidate(EInvalidateWidgetReason::Paint);
		}
	}

	Invalidate(EInvalidateWidgetReason::Paint);
	return true;
}

int32 SRootMotionEditor::GetFrameFromTime() const
{
	if (CachedTiming.TotalDurationSeconds <= 0.0f) return 0;
	return RootMotionEditorInternal::TimeToFrameIndex(CachedTiming, PlaybackTime);
}

// ==========================================
// KEYBOARD
// ==========================================

FReply SRootMotionEditor::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (!CanMutateLiveSelection())
	{
		return FReply::Unhandled();
	}
	// Guard: don't handle when text input is focused
	if (Paper2DPlusEditor::SlateShortcutUtils::ShouldIgnoreShortcutForFocusedWidget())
	{
		return FReply::Unhandled();
	}

	const FKey Key = InKeyEvent.GetKey();

	// Guard Ctrl for shortcuts (Ctrl+S save, etc.)
	if (InKeyEvent.IsControlDown())
	{
		return FReply::Unhandled();
	}

	// Space: toggle queue playback if queue active, else single-flipbook playback
	if (Key == EKeys::SpaceBar)
	{
		if (Model.IsValid() && Model->IsQueueActive())
			Model->SetQueuePlaying(!Model->IsQueuePlaying());
		else
			TogglePlayback();
		return FReply::Handled();
	}

	// O: toggle backward onion skin
	if (Key == EKeys::O)
	{
		bShowOnionSkin = !bShowOnionSkin;
		if (MotionCanvas.IsValid()) MotionCanvas->Invalidate(EInvalidateWidgetReason::Paint);
		return FReply::Handled();
	}

	// F: toggle forward onion skin
	if (Key == EKeys::F)
	{
		bShowForwardOnionSkin = !bShowForwardOnionSkin;
		if (MotionCanvas.IsValid()) MotionCanvas->Invalidate(EInvalidateWidgetReason::Paint);
		return FReply::Handled();
	}

	// Up/Down: queue-aware flipbook navigation
	if (Key == EKeys::Up && Model.IsValid())
	{
		if (Model->StepQueue(-1) == INDEX_NONE)
		{
			int32 NewIdx = Model->GetVisualAdjacentFlipbookIndex(-1);
			if (NewIdx != INDEX_NONE) Model->SetSelectedFlipbook(NewIdx);
		}
		return FReply::Handled();
	}
	if (Key == EKeys::Down && Model.IsValid())
	{
		if (Model->StepQueue(1) == INDEX_NONE)
		{
			int32 NewIdx = Model->GetVisualAdjacentFlipbookIndex(1);
			if (NewIdx != INDEX_NONE) Model->SetSelectedFlipbook(NewIdx);
		}
		return FReply::Handled();
	}

	// Left/Right: queue-aware frame navigation
	if (Key == EKeys::Left && GetFrameCount() > 0)
	{
		if (SelectedFrameIndex > 0)
		{
			OnFrameClicked(SelectedFrameIndex - 1);
		}
		else if (Model.IsValid())
		{
			// StepQueue lands on the previous animation's LAST frame; without a queue entry to step
			// to, wrap inside this flipbook instead of swallowing the key.
			if (Model->StepQueue(-1, /*bLandOnLastFrame=*/true) == INDEX_NONE)
			{
				const int32 FrameCount = GetFrameCount();
				if (FrameCount > 1) OnFrameClicked(FrameCount - 1);
			}
		}
		return FReply::Handled();
	}
	if (Key == EKeys::Right && GetFrameCount() > 0)
	{
		if (SelectedFrameIndex < GetFrameCount() - 1)
		{
			OnFrameClicked(SelectedFrameIndex + 1);
		}
		else if (Model.IsValid())
		{
			if (Model->StepQueue(1) == INDEX_NONE)
			{
				if (GetFrameCount() > 1) OnFrameClicked(0);
			}
		}
		return FReply::Handled();
	}

	// WASD nudge for pixel-precise positioning
	FVector2D NudgeDelta = FVector2D::ZeroVector;
	const float NudgeAmount = InKeyEvent.IsShiftDown() ? 16.0f : 1.0f;

	if (Key == EKeys::A) NudgeDelta.X = -NudgeAmount;
	else if (Key == EKeys::D) NudgeDelta.X = NudgeAmount;
	else if (Key == EKeys::W) NudgeDelta.Y = -NudgeAmount;
	else if (Key == EKeys::S) NudgeDelta.Y = NudgeAmount;

	if (!NudgeDelta.IsNearlyZero())
	{
		NudgeCurrentFrame(NudgeDelta);
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

void SRootMotionEditor::NudgeCurrentFrame(FVector2D Delta)
{
	if (Delta.IsNearlyZero() || !CanMutateLiveSelection()) return;
	FFlipbookProfileEntry* Data = GetSelectedFlipbookData();
	if (!Data) return;

	// Begin before growing the array so Ctrl+Z can restore the empty pre-authoring state (F29).
	if (!ActiveTransaction.IsValid())
	{
		BeginTransaction(LOCTEXT("NudgeRootMotion", "Nudge Root Motion"));
	}
	if (!ActiveTransaction.IsValid() || !DoesActiveEditTargetLiveSelection()) return;
	EnsureRootMotionArraySized();
	Data = GetSelectedFlipbookData();
	if (!Data || !Data->MotionData.RootMotion.IsValidIndex(SelectedFrameIndex)) return;

	Data->MotionData.RootMotion[SelectedFrameIndex].Position += Delta;
	MarkActiveTransactionChanged();
	if (TSharedPtr<FActiveTimerHandle> OldTimer = NudgeDebounceTimer.Pin())
	{
		UnRegisterActiveTimer(OldTimer.ToSharedRef());
	}
	NudgeDebounceTimer = RegisterActiveTimer(
		0.5f,
		FWidgetActiveTimerDelegate::CreateSP(this, &SRootMotionEditor::HandleNudgeDebounce));

	if (MotionCanvas.IsValid()) MotionCanvas->Invalidate(EInvalidateWidgetReason::Paint);
	if (FrameStripBox.IsValid()) FrameStripBox->Invalidate(EInvalidateWidgetReason::Paint);
	if (PropertiesBox.IsValid()) PropertiesBox->Invalidate(EInvalidateWidgetReason::Paint);
}

void SRootMotionEditor::CommitNudgeTransaction()
{
	NudgeDebounceTimer.Reset();
	EndTransaction();
	if (FrameStripBox.IsValid()) FrameStripBox->Invalidate(EInvalidateWidgetReason::Paint);
}

EActiveTimerReturnType SRootMotionEditor::HandleNudgeDebounce(double CurrentTime, float DeltaTime)
{
	(void)CurrentTime;
	(void)DeltaTime;
	CommitNudgeTransaction();
	return EActiveTimerReturnType::Stop;
}

// ==========================================
// EXTERNAL CONTROL
// ==========================================

void SRootMotionEditor::OnModelFrameSelected()
{
	const int32 NewFrame = Model.IsValid() ? Model->GetSelectedFrameIndex() : SelectedFrameIndex;
	if (NewFrame == SelectedFrameIndex)
	{
		return;
	}
	// A picker/timeline change may arrive while a spinbox, nudge debounce, or canvas drag owns the
	// transaction. Finish it before any input can mutate the new frame under the old gesture identity.
	FinishActiveEditGesture(/*bReleaseCanvasCapture=*/true);
	SelectedFrameIndex = NewFrame;
	if (FrameStripBox.IsValid()) FrameStripBox->Invalidate(EInvalidateWidgetReason::Paint);
	if (PropertiesBox.IsValid()) PropertiesBox->Invalidate(EInvalidateWidgetReason::Paint);
	if (MotionCanvas.IsValid()) MotionCanvas->Invalidate(EInvalidateWidgetReason::Paint);
}

void SRootMotionEditor::OnModelFlipbookSelected(int32 FlipbookIndex)
{
	if (SelectedFlipbookIndex == FlipbookIndex) return;

	// Every edit mode shares one transaction owner. Finish it before changing animation identity so a
	// delayed spinbox/canvas callback cannot write the newly selected animation (F30/U25).
	FinishActiveEditGesture(/*bReleaseCanvasCapture=*/true);
	StopPlayback();
	SelectedFlipbookIndex = FlipbookIndex;
	SelectedFrameIndex = Model.IsValid() ? Model->GetSelectedFrameIndex() : 0;

	LerpStartFrame = 0;
	LerpEndFrame = 0;
	FFlipbookProfileEntry* Data = GetSelectedFlipbookData();
	if (Data && Data->MotionData.HasRootMotion())
	{
		int32 First = INDEX_NONE, Last = INDEX_NONE;
		for (int32 i = 0; i < Data->MotionData.RootMotion.Num(); i++)
		{
			if (!Data->MotionData.RootMotion[i].Position.IsNearlyZero())
			{
				if (First == INDEX_NONE) First = i;
				Last = i;
			}
		}
		if (First != INDEX_NONE) { LerpStartFrame = First; LerpEndFrame = Last; }
		else { LerpEndFrame = FMath::Max(0, Data->MotionData.RootMotion.Num() - 1); }
	}

	RefreshAll();
}

void SRootMotionEditor::RefreshAll()
{
	// Re-resolve the asset from the shared model so a Base-Profile swap in the Character Layer editor
	// retargets this tab to the current profile (mirrors SHitboxEditorPanel::RefreshAll). Harmless in
	// the Character Profile editor, where the asset never swaps.
	if (Model.IsValid()) { Asset = Model->GetAsset(); }
	bNeedsRefresh = false;
	RefreshFrameStrip();
	RefreshPropertiesPanel();
	if (MotionCanvas.IsValid()) MotionCanvas->Invalidate(EInvalidateWidgetReason::Paint);
}

// ==========================================
// HELPERS
// ==========================================

FFlipbookProfileEntry* SRootMotionEditor::GetSelectedFlipbookData() const
{
	if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex)) return nullptr;
	return &Asset->Flipbooks[SelectedFlipbookIndex];
}

UPaperFlipbook* SRootMotionEditor::GetSelectedFlipbook() const
{
	FFlipbookProfileEntry* Data = GetSelectedFlipbookData();
	if (!Data || Data->Identity.Flipbook.IsNull()) return nullptr;
	return Data->Identity.Flipbook.LoadSynchronous();
}

int32 SRootMotionEditor::GetFrameCount() const
{
	UPaperFlipbook* FB = GetSelectedFlipbook();
	return FB ? FB->GetNumKeyFrames() : 0;
}

// ==========================================
// SRootMotionCanvas — IMPLEMENTATION
// ==========================================

void SRootMotionCanvas::Construct(const FArguments& InArgs)
{
	Asset = InArgs._Asset;
	SelectedFlipbookIndex = InArgs._SelectedFlipbookIndex;
	SelectedFrameIndex = InArgs._SelectedFrameIndex;
	IsPlaying = InArgs._IsPlaying;
	PlaybackTime = InArgs._PlaybackTime;
	ShowOnionSkin = InArgs._ShowOnionSkin;
	ShowForwardOnionSkin = InArgs._ShowForwardOnionSkin;
	OnionSkinFrames = InArgs._OnionSkinFrames;
	OnionSkinOpacity = InArgs._OnionSkinOpacity;
	PreviousFlipbookIndex = InArgs._PreviousFlipbookIndex;
	NextFlipbookIndex = InArgs._NextFlipbookIndex;

	SetClipping(EWidgetClipping::ClipToBounds);
}

FVector2D SRootMotionCanvas::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	return FVector2D(400.f, 300.f);
}

// ==========================================
// COORDINATE CONVERSION
// ==========================================

float SRootMotionCanvas::GetEffectiveZoom(const FGeometry& Geom) const
{
	if (PaintZoomOverride.IsSet())
	{
		return PaintZoomOverride.GetValue();
	}
	// Compute zoom based on motion path bounds + sprite size
	FVector2D LargestDims = GetLargestSpriteDims();
	FVector2D WidgetSize = Geom.GetLocalSize();

	// Get motion path bounds to include in zoom calculation
	FVector2D ContentSize = LargestDims;

	if (Asset.IsValid())
	{
		int32 FBIndex = SelectedFlipbookIndex.Get(INDEX_NONE);
		if (Asset->Flipbooks.IsValidIndex(FBIndex))
		{
			const FFlipbookProfileEntry& Anim = Asset->Flipbooks[FBIndex];
			if (Anim.MotionData.HasRootMotion())
			{
				FVector2D PathBounds = GetMotionPathBounds(Anim.MotionData.RootMotion);
				// Add sprite padding to path bounds
				ContentSize.X = FMath::Max(ContentSize.X, PathBounds.X + LargestDims.X);
				ContentSize.Y = FMath::Max(ContentSize.Y, PathBounds.Y + LargestDims.Y);
			}
		}
	}

	if (ContentSize.X <= 0 || ContentSize.Y <= 0) return UserZoom;

	float BaseScale = FMath::Min(WidgetSize.X / ContentSize.X, WidgetSize.Y / ContentSize.Y) * 0.8f;
	return BaseScale * UserZoom;
}

FVector2D SRootMotionCanvas::GetCanvasOrigin(const FGeometry& Geom) const
{
	return Geom.GetLocalSize() * 0.5f + PanOffset;
}

FVector2D SRootMotionCanvas::ScreenToCanvas(const FGeometry& Geom, const FVector2D& ScreenPos) const
{
	float Zoom = GetEffectiveZoom(Geom);
	if (Zoom <= 0.f) return FVector2D::ZeroVector;
	return (ScreenPos - GetCanvasOrigin(Geom)) / Zoom;
}

FVector2D SRootMotionCanvas::CanvasToScreen(const FGeometry& Geom, const FVector2D& CanvasPos) const
{
	return CanvasPos * GetEffectiveZoom(Geom) + GetCanvasOrigin(Geom);
}

int32 SRootMotionCanvas::SnapToGrid(int32 Value) const
{
	return FMath::RoundToInt((float)Value / GridSize) * GridSize;
}

// ==========================================
// ON PAINT
// ==========================================

int32 SRootMotionCanvas::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	// Checkerboard background
	FEditorCanvasUtils::DrawCheckerboard(OutDrawElements, LayerId, AllottedGeometry, 16.0f);
	LayerId++;

	if (!Asset.IsValid()) return LayerId;
	int32 FBIndex = SelectedFlipbookIndex.Get(INDEX_NONE);
	if (!Asset->Flipbooks.IsValidIndex(FBIndex)) return LayerId;

	const FFlipbookProfileEntry& Anim = Asset->Flipbooks[FBIndex];
	UPaperFlipbook* FB = Anim.Identity.Flipbook.Get();
	if (!FB || FB->GetNumKeyFrames() == 0) return LayerId;

	int32 FrameIdx = SelectedFrameIndex.Get(0);
	FrameIdx = FMath::Clamp(FrameIdx, 0, FB->GetNumKeyFrames() - 1);
	// CanvasToScreen is called many times by the grid, path, skins, and sprite helpers. Cache the
	// fit calculation for this paint so a long motion path is scanned once, not once per draw point.
	PaintZoomOverride = GetEffectiveZoom(AllottedGeometry);

	// Draw grid
	DrawGrid(AllottedGeometry, OutDrawElements, LayerId);
	LayerId++;

	// Draw ground line
	DrawGroundLine(AllottedGeometry, OutDrawElements, LayerId);
	LayerId++;

	// Draw motion path
	if (Anim.MotionData.HasRootMotion())
	{
		DrawMotionPath(AllottedGeometry, OutDrawElements, LayerId, Anim.MotionData.RootMotion, FrameIdx);
		LayerId++;
	}

	// Draw onion skins (behind current sprite)
	if (ShowOnionSkin.Get())
	{
		DrawOnionSkin(AllottedGeometry, OutDrawElements, LayerId, FB, Anim.MotionData.RootMotion, FrameIdx);
		LayerId++;
	}

	// Draw forward onion skin
	if (ShowForwardOnionSkin.Get())
	{
		DrawForwardOnionSkin(AllottedGeometry, OutDrawElements, LayerId, FB, Anim.MotionData.RootMotion, FrameIdx);
		LayerId++;
	}

	// Draw sprite at current frame's root motion offset
	FVector2D Offset = FVector2D::ZeroVector;
	if (Anim.MotionData.RootMotion.IsValidIndex(FrameIdx))
	{
		Offset = Anim.MotionData.RootMotion[FrameIdx].Position;
	}
	DrawSprite(AllottedGeometry, OutDrawElements, LayerId, FB, FrameIdx, Offset);
	LayerId++;

	// Draw origin marker (small crosshair at 0,0)
	{
		const FSlateBrush* WhiteBrush = FAppStyle::Get().GetBrush("WhiteBrush");
		FVector2D OriginScreen = CanvasToScreen(AllottedGeometry, FVector2D::ZeroVector);
		FLinearColor OriginColor(0.8f, 0.8f, 0.2f, 0.6f);
		float CrossSize = 8.0f;

		// Horizontal
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
			MakePaintGeometry(AllottedGeometry, FVector2D(CrossSize * 2, 1), FSlateLayoutTransform(FVector2D(OriginScreen.X - CrossSize, OriginScreen.Y))),
			WhiteBrush, ESlateDrawEffect::None, OriginColor);
		// Vertical
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
			MakePaintGeometry(AllottedGeometry, FVector2D(1, CrossSize * 2), FSlateLayoutTransform(FVector2D(OriginScreen.X, OriginScreen.Y - CrossSize))),
			WhiteBrush, ESlateDrawEffect::None, OriginColor);
		LayerId++;
	}

	PaintZoomOverride.Reset();
	return LayerId;
}

// ==========================================
// DRAW HELPERS
// ==========================================

void SRootMotionCanvas::DrawGroundLine(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
	const FSlateBrush* WhiteBrush = FAppStyle::Get().GetBrush("WhiteBrush");
	FVector2D Origin = GetCanvasOrigin(Geom);
	FVector2D WidgetSize = Geom.GetLocalSize();

	// Horizontal ground line at Y=0 (canvas origin)
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
		MakePaintGeometry(Geom, FVector2D(WidgetSize.X, 1), FSlateLayoutTransform(FVector2D(0, Origin.Y))),
		WhiteBrush, ESlateDrawEffect::None, FLinearColor(0.3f, 0.5f, 0.3f, 0.4f));
}

void SRootMotionCanvas::DrawGrid(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
	const FSlateBrush* WhiteBrush = FAppStyle::Get().GetBrush("WhiteBrush");
	float Zoom = GetEffectiveZoom(Geom);
	if (Zoom <= 0.01f) return;

	FVector2D Origin = GetCanvasOrigin(Geom);
	FVector2D WidgetSize = Geom.GetLocalSize();
	float GridScreenSize = GridSize * Zoom;

	// Skip grid if too dense
	if (GridScreenSize < 4.0f) return;

	FLinearColor GridColor(0.15f, 0.15f, 0.15f, 0.3f);

	// Vertical lines
	float StartX = FMath::Fmod(Origin.X, GridScreenSize);
	if (StartX < 0) StartX += GridScreenSize;
	for (float X = StartX; X < WidgetSize.X; X += GridScreenSize)
	{
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
			MakePaintGeometry(Geom, FVector2D(1, WidgetSize.Y), FSlateLayoutTransform(FVector2D(X, 0))),
			WhiteBrush, ESlateDrawEffect::None, GridColor);
	}

	// Horizontal lines
	float StartY = FMath::Fmod(Origin.Y, GridScreenSize);
	if (StartY < 0) StartY += GridScreenSize;
	for (float Y = StartY; Y < WidgetSize.Y; Y += GridScreenSize)
	{
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
			MakePaintGeometry(Geom, FVector2D(WidgetSize.X, 1), FSlateLayoutTransform(FVector2D(0, Y))),
			WhiteBrush, ESlateDrawEffect::None, GridColor);
	}
}

void SRootMotionCanvas::DrawMotionPath(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const TArray<FRootMotionFrameData>& RootMotion, int32 CurrentFrame) const
{
	if (RootMotion.Num() < 2) return;

	const FSlateBrush* WhiteBrush = FAppStyle::Get().GetBrush("WhiteBrush");

	// Build polyline
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 4
	TArray<FVector2D> PathPoints;
#else
	TArray<FVector2f> PathPoints;
#endif
	PathPoints.Reserve(RootMotion.Num());
	for (int32 i = 0; i < RootMotion.Num(); i++)
	{
		FVector2D ScreenPos = CanvasToScreen(Geom, RootMotion[i].Position);
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 4
		PathPoints.Add(FVector2D(ScreenPos.X, ScreenPos.Y));
#else
		PathPoints.Add(FVector2f(ScreenPos.X, ScreenPos.Y));
#endif
	}

	// Draw the path line
	FLinearColor PathColor(0.4f, 0.7f, 1.0f, 0.6f);
	FSlateDrawElement::MakeLines(OutDrawElements, LayerId,
		Geom.ToPaintGeometry(), PathPoints, ESlateDrawEffect::None, PathColor, true, 2.0f);

	// Draw dots at each frame position with frame index labels
	for (int32 i = 0; i < RootMotion.Num(); i++)
	{
		FVector2D ScreenPos = CanvasToScreen(Geom, RootMotion[i].Position);
		bool bIsCurrent = (i == CurrentFrame);
		float DotSize = bIsCurrent ? 10.0f : 6.0f;
		FLinearColor DotColor = bIsCurrent
			? FLinearColor(0.2f, 0.9f, 0.3f, 1.0f)  // Green for current
			: FLinearColor(0.5f, 0.5f, 0.5f, 0.7f);  // Gray for others

		FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1,
			MakePaintGeometry(Geom, FVector2D(DotSize, DotSize),
				FSlateLayoutTransform(FVector2D(ScreenPos.X - DotSize * 0.5f, ScreenPos.Y - DotSize * 0.5f))),
			WhiteBrush, ESlateDrawEffect::None, DotColor);

		// Frame index label (above the dot)
		FSlateFontInfo LabelFont = FCoreStyle::GetDefaultFontStyle("Regular", 7);
		FSlateDrawElement::MakeText(OutDrawElements, LayerId + 2,
			Geom.ToPaintGeometry(FSlateLayoutTransform(FVector2D(ScreenPos.X - 4, ScreenPos.Y - DotSize - 12))),
			FString::FromInt(i),
			LabelFont,
			ESlateDrawEffect::None,
			bIsCurrent ? FLinearColor(0.2f, 0.9f, 0.3f, 1.0f) : FLinearColor(0.6f, 0.6f, 0.6f, 0.6f));
	}
}

void SRootMotionCanvas::DrawSprite(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	UPaperFlipbook* Flipbook, int32 FrameIndex, const FVector2D& Offset, const FLinearColor& Tint) const
{
	if (!Flipbook || Flipbook->GetNumKeyFrames() == 0) return;
	FrameIndex = FMath::Clamp(FrameIndex, 0, Flipbook->GetNumKeyFrames() - 1);

	const FPaperFlipbookKeyFrame& KeyFrame = Flipbook->GetKeyFrameChecked(FrameIndex);
	UPaperSprite* Sprite = KeyFrame.Sprite;
	if (!Sprite) return;

	UTexture2D* Texture = Sprite->GetBakedTexture();
	if (!Texture) return;

	const FVector2D SpriteSourcePos = FVector2D(Sprite->GetSourceUV());
	const FVector2D SpriteSourceSize = FVector2D(Sprite->GetSourceSize());

	float Zoom = GetEffectiveZoom(Geom);
	FVector2D DrawSize = SpriteSourceSize * Zoom;
	FVector2D DrawPos = CanvasToScreen(Geom, Offset) - DrawSize * 0.5f;

	// Apply per-frame sprite offset from extraction info + pivot shift (prevents sprite bouncing)
	FVector2D SpriteShift = FVector2D::ZeroVector;
	if (Asset.IsValid())
	{
		int32 FBIndex = SelectedFlipbookIndex.Get(INDEX_NONE);
		if (Asset->Flipbooks.IsValidIndex(FBIndex))
		{
			const FFlipbookProfileEntry& Anim = Asset->Flipbooks[FBIndex];
			if (Anim.CombatData.FrameExtractionInfo.IsValidIndex(FrameIndex))
			{
				SpriteShift = FVector2D(Anim.CombatData.FrameExtractionInfo[FrameIndex].SpriteOffset);
			}
		}
	}

	// Pivot shift: how much the sprite's custom pivot offsets it from center
	FVector2D SourceCenter = Sprite->GetSourceUV() + Sprite->GetSourceSize() * 0.5f;
	FVector2D PivotPos = Sprite->GetPivotPosition();
	FVector2D PivotShift = FVector2D(SourceCenter) - FVector2D(PivotPos);

	DrawPos.X += (SpriteShift.X + PivotShift.X) * Zoom;
	DrawPos.Y += (SpriteShift.Y + PivotShift.Y) * Zoom;

	FSlateBrush SpriteBrush;
	SpriteBrush.SetResourceObject(Texture);
	SpriteBrush.ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
	SpriteBrush.SetUVRegion(FBox2D(
		SpriteSourcePos / SpriteBrush.ImageSize,
		(SpriteSourcePos + SpriteSourceSize) / SpriteBrush.ImageSize));

	FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
		MakePaintGeometry(Geom, DrawSize, FSlateLayoutTransform(DrawPos)),
		&SpriteBrush, ESlateDrawEffect::None, Tint);
}

void SRootMotionCanvas::DrawOnionSkin(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	UPaperFlipbook* Flipbook, const TArray<FRootMotionFrameData>& RootMotion, int32 CurrentFrame) const
{
	int32 NumOnionFrames = OnionSkinFrames.Get();
	float BaseOpacity = OnionSkinOpacity.Get();

	int32 FramesDrawn = 0;

	for (int32 i = 1; i <= NumOnionFrames; i++)
	{
		int32 PrevFrame = CurrentFrame - i;
		if (PrevFrame < 0) break;

		FVector2D Offset = FVector2D::ZeroVector;
		if (RootMotion.IsValidIndex(PrevFrame))
		{
			Offset = RootMotion[PrevFrame].Position;
		}

		float Opacity = BaseOpacity * (1.0f - (float)(FramesDrawn) / (float)NumOnionFrames);
		FLinearColor OnionColor(0.5f, 0.5f, 1.0f, Opacity);

		DrawSprite(Geom, OutDrawElements, LayerId, Flipbook, PrevFrame, Offset, OnionColor);
		FramesDrawn++;
	}

	int32 RemainingFrames = NumOnionFrames - FramesDrawn;
	int32 PrevFBIdx = PreviousFlipbookIndex.Get();

	if (RemainingFrames > 0 && PrevFBIdx != INDEX_NONE && Asset.IsValid()
		&& Asset->Flipbooks.IsValidIndex(PrevFBIdx))
	{
		const FFlipbookProfileEntry& PrevAnim = Asset->Flipbooks[PrevFBIdx];
		UPaperFlipbook* PrevFB = PrevAnim.Identity.Flipbook.Get();

		if (PrevFB && PrevFB->GetNumKeyFrames() > 0)
		{
			const TArray<FRootMotionFrameData>& PrevMotion = PrevAnim.MotionData.RootMotion;
			int32 PrevFrameCount = PrevFB->GetNumKeyFrames();

			for (int32 i = 0; i < RemainingFrames; i++)
			{
				int32 FrameIdx = PrevFrameCount - 1 - i;
				if (FrameIdx < 0) break;

				FVector2D Offset = FVector2D::ZeroVector;
				if (PrevMotion.IsValidIndex(FrameIdx))
				{
					Offset = PrevMotion[FrameIdx].Position;
				}

				float Opacity = BaseOpacity * (1.0f - (float)(FramesDrawn) / (float)NumOnionFrames);
				FLinearColor OnionColor(0.7f, 0.4f, 1.0f, Opacity);

				DrawSprite(Geom, OutDrawElements, LayerId, PrevFB, FrameIdx, Offset, OnionColor);
				FramesDrawn++;
			}
		}
	}
}

void SRootMotionCanvas::DrawForwardOnionSkin(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	UPaperFlipbook* Flipbook, const TArray<FRootMotionFrameData>& RootMotion, int32 CurrentFrame) const
{
	int32 NumOnionFrames = OnionSkinFrames.Get();
	float BaseOpacity = OnionSkinOpacity.Get();
	int32 TotalFrames = Flipbook->GetNumKeyFrames();

	int32 FramesDrawn = 0;

	for (int32 i = 1; i <= NumOnionFrames; i++)
	{
		int32 NextFrame = CurrentFrame + i;
		if (NextFrame >= TotalFrames) break;

		FVector2D Offset = FVector2D::ZeroVector;
		if (RootMotion.IsValidIndex(NextFrame))
		{
			Offset = RootMotion[NextFrame].Position;
		}

		float Opacity = BaseOpacity * (1.0f - (float)(FramesDrawn) / (float)NumOnionFrames);
		FLinearColor OnionColor(0.5f, 1.0f, 0.5f, Opacity);

		DrawSprite(Geom, OutDrawElements, LayerId, Flipbook, NextFrame, Offset, OnionColor);
		FramesDrawn++;
	}

	int32 RemainingFrames = NumOnionFrames - FramesDrawn;
	int32 NextFBIdx = NextFlipbookIndex.Get();

	if (RemainingFrames > 0 && NextFBIdx != INDEX_NONE && Asset.IsValid()
		&& Asset->Flipbooks.IsValidIndex(NextFBIdx))
	{
		const FFlipbookProfileEntry& NextAnim = Asset->Flipbooks[NextFBIdx];
		UPaperFlipbook* NextFB = NextAnim.Identity.Flipbook.Get();

		if (NextFB && NextFB->GetNumKeyFrames() > 0)
		{
			const TArray<FRootMotionFrameData>& NextMotion = NextAnim.MotionData.RootMotion;

			for (int32 i = 0; i < RemainingFrames; i++)
			{
				if (i >= NextFB->GetNumKeyFrames()) break;

				FVector2D Offset = FVector2D::ZeroVector;
				if (NextMotion.IsValidIndex(i))
				{
					Offset = NextMotion[i].Position;
				}

				float Opacity = BaseOpacity * (1.0f - (float)(FramesDrawn) / (float)NumOnionFrames);
				FLinearColor OnionColor(0.7f, 0.4f, 1.0f, Opacity);

				DrawSprite(Geom, OutDrawElements, LayerId, NextFB, i, Offset, OnionColor);
				FramesDrawn++;
			}
		}
	}
}

// ==========================================
// MOUSE INPUT
// ==========================================

FReply SRootMotionCanvas::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		DragMode = EDragMode::Panning;
		PanStartPos = MouseEvent.GetScreenSpacePosition();
		PanStartOffset = PanOffset;
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}

	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		if (!Asset.IsValid()) return FReply::Handled();
		int32 FBIndex = SelectedFlipbookIndex.Get(INDEX_NONE);
		if (!Asset->Flipbooks.IsValidIndex(FBIndex)) return FReply::Handled();

		// Enter pending drag — actual drag starts after dead zone
		DragMode = EDragMode::PendingDrag;
		DragStartScreenPos = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
		DragStartCanvasPos = ScreenToCanvas(MyGeometry, DragStartScreenPos);

		const FFlipbookProfileEntry& Anim = Asset->Flipbooks[FBIndex];
		int32 FrameIdx = SelectedFrameIndex.Get(0);
		DragStartPosition = (Anim.MotionData.RootMotion.IsValidIndex(FrameIdx))
			? Anim.MotionData.RootMotion[FrameIdx].Position : FVector2D::ZeroVector;

		return FReply::Handled().CaptureMouse(SharedThis(this));
	}

	return FReply::Unhandled();
}

FReply SRootMotionCanvas::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (DragMode == EDragMode::Panning && MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		DragMode = EDragMode::None;
		return FReply::Handled().ReleaseMouseCapture();
	}

	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		if (DragMode == EDragMode::PendingDrag)
		{
			// Click without drag — just focus the widget
			DragMode = EDragMode::None;
			return FReply::Handled().ReleaseMouseCapture();
		}
		if (DragMode == EDragMode::MovingSprite)
		{
			DragMode = EDragMode::None;
			OnDragEnded.ExecuteIfBound();
			return FReply::Handled().ReleaseMouseCapture();
		}
	}

	return FReply::Unhandled();
}

FReply SRootMotionCanvas::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!HasMouseCapture())
	{
		return FReply::Unhandled();
	}
	FVector2D LocalPos = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());

	if (DragMode == EDragMode::Panning)
	{
		FVector2D Delta = MouseEvent.GetScreenSpacePosition() - PanStartPos;
		PanOffset = PanStartOffset + Delta;
		Invalidate(EInvalidateWidgetReason::Paint);
		return FReply::Handled();
	}

	if (DragMode == EDragMode::PendingDrag)
	{
		// Check dead zone before starting actual drag
		FVector2D Delta = LocalPos - DragStartScreenPos;
		if (Delta.Size() >= DragDeadZone)
		{
			DragMode = EDragMode::MovingSprite;
			OnDragStarted.ExecuteIfBound();
		}
		return FReply::Handled();
	}

	if (DragMode == EDragMode::MovingSprite)
	{
		FVector2D CurrentCanvasPos = ScreenToCanvas(MyGeometry, LocalPos);
		FVector2D CanvasDelta = CurrentCanvasPos - DragStartCanvasPos;
		FVector2D NewPos = DragStartPosition + CanvasDelta;

		// Grid snap
		NewPos.X = SnapToGrid(FMath::RoundToInt(NewPos.X));
		NewPos.Y = SnapToGrid(FMath::RoundToInt(NewPos.Y));

		OnPositionChanged.ExecuteIfBound(NewPos);
		Invalidate(EInvalidateWidgetReason::Paint);
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

FReply SRootMotionCanvas::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	float Delta = MouseEvent.GetWheelDelta() * 0.1f;
	UserZoom = FMath::Clamp(UserZoom + Delta, 0.1f, 5.0f);
	Invalidate(EInvalidateWidgetReason::Paint);
	return FReply::Handled();
}

void SRootMotionCanvas::OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	if (DragMode == EDragMode::MovingSprite)
	{
		DragMode = EDragMode::None;
		OnDragEnded.ExecuteIfBound();
	}
	else
	{
		DragMode = EDragMode::None;
	}
	Invalidate(EInvalidateWidgetReason::Paint);
	SLeafWidget::OnMouseCaptureLost(CaptureLostEvent);
}

FCursorReply SRootMotionCanvas::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	if (DragMode == EDragMode::Panning) return FCursorReply::Cursor(EMouseCursor::GrabHandClosed);
	if (DragMode == EDragMode::MovingSprite) return FCursorReply::Cursor(EMouseCursor::CardinalCross);
	return FCursorReply::Unhandled();
}

// ==========================================
// HELPERS
// ==========================================

FVector2D SRootMotionCanvas::GetLargestSpriteDims() const
{
	if (!Asset.IsValid()) return CachedLargestDims;
	int32 FBIndex = SelectedFlipbookIndex.Get(INDEX_NONE);
	if (!Asset->Flipbooks.IsValidIndex(FBIndex)) return CachedLargestDims;

	const FFlipbookProfileEntry& Anim = Asset->Flipbooks[FBIndex];
	UPaperFlipbook* FB = Anim.Identity.Flipbook.Get();
	if (!FB) return CachedLargestDims;

	// Cache check
	if (FBIndex == CachedLargestDimsFlipbookIndex && CachedLargestDimsFlipbook.IsValid() && CachedLargestDimsFlipbook.Get() == FB)
	{
		return CachedLargestDims;
	}

	FVector2D Largest(128, 128);
	for (int32 i = 0; i < FB->GetNumKeyFrames(); i++)
	{
		const FPaperFlipbookKeyFrame& KeyFrame = FB->GetKeyFrameChecked(i);
		if (KeyFrame.Sprite)
		{
			FVector2D Size = FVector2D(KeyFrame.Sprite->GetSourceSize());
			Largest.X = FMath::Max(Largest.X, Size.X);
			Largest.Y = FMath::Max(Largest.Y, Size.Y);
		}
	}

	CachedLargestDims = Largest;
	CachedLargestDimsFlipbookIndex = FBIndex;
	CachedLargestDimsFlipbook = FB;
	return Largest;
}

FVector2D SRootMotionCanvas::GetMotionPathBounds(const TArray<FRootMotionFrameData>& RootMotion) const
{
	if (RootMotion.Num() == 0) return FVector2D::ZeroVector;

	FVector2D Min(TNumericLimits<double>::Max());
	FVector2D Max(TNumericLimits<double>::Lowest());

	for (const FRootMotionFrameData& Frame : RootMotion)
	{
		Min.X = FMath::Min(Min.X, Frame.Position.X);
		Min.Y = FMath::Min(Min.Y, Frame.Position.Y);
		Max.X = FMath::Max(Max.X, Frame.Position.X);
		Max.Y = FMath::Max(Max.Y, Frame.Position.Y);
	}

	return Max - Min;
}

#undef LOCTEXT_NAMESPACE
