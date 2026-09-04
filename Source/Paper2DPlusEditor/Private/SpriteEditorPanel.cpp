// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "SpriteEditorPanel.h"
#include "CharacterProfileAssetEditor.h"
#include "CharacterProfileEditorModel.h"
#include "EditorCanvasUtils.h"
#include "ProfilePropertyRow.h"
#include "SlateShortcutUtils.h"
#include "Input/DragAndDrop.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"  // explicit (transitive only on UE 5.3+; 5.0-5.2 need it — cross-version)
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSlider.h"
#include "Framework/Application/SlateApplication.h"
#include "PaperFlipbook.h"
#include "PaperSprite.h"
#include "SSpriteEditorDragDropWidgets.h"
#include "Editor.h"

#define LOCTEXT_NAMESPACE "SpriteEditorPanel"

namespace Paper2DPlusEditor::SpriteEditorPanelUtils
{
int32 PrepareFrameStripFrameCount(UPaper2DPlusCharacterProfileAsset* Asset, int32 FlipbookIndex)
{
	if (!Asset || !Asset->Flipbooks.IsValidIndex(FlipbookIndex))
	{
		return 0;
	}

	Asset->SyncFramesToFlipbook(FlipbookIndex, /*bGrowOnly=*/true);

	const FFlipbookProfileEntry& Anim = Asset->Flipbooks[FlipbookIndex];
	if (!Anim.Identity.Flipbook.IsNull())
	{
		if (UPaperFlipbook* Flipbook = Anim.Identity.Flipbook.LoadSynchronous())
		{
			return Flipbook->GetNumKeyFrames();
		}
	}

	return Anim.CombatData.Frames.Num();
}

}

Paper2DPlusEditor::DirectionalPreviewPlayback::FDecision
Paper2DPlusEditor::DirectionalPreviewPlayback::Resolve(
	const bool bIsPlaying,
	const bool bResumeAfterDirectionalPreviewResolves,
	const EEvent Event)
{
	switch (Event)
	{
	case EEvent::BeginResolving:
	case EEvent::BecameEmpty:
		return { false, bIsPlaying || bResumeAfterDirectionalPreviewResolves };
	case EEvent::BecameRenderable:
		return { bIsPlaying || bResumeAfterDirectionalPreviewResolves, false };
	case EEvent::BecameUnavailable:
	case EEvent::UserToggleWhilePaused:
		return { false, false };
	case EEvent::UserToggle:
		return { !bIsPlaying, false };
	default:
		checkNoEntry();
		return { false, false };
	}
}

// ==========================================
// Anonymous helpers
// ==========================================
namespace
{
int32 FindSpriteEditorFrameIndexBySourceIndex(const FFlipbookProfileEntry& Anim, int32 SourceFrameIndex)
{
	for (int32 Index = 0; Index < Anim.CombatData.FrameExtractionInfo.Num(); ++Index)
	{
		if (Anim.CombatData.FrameExtractionInfo[Index].SourceFrameIndex == SourceFrameIndex)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}
}

const FName SSpriteEditorPanel::OffsetNudgePanelId(TEXT("Paper2DPlus.Sprite.OffsetNudge"));
const FName SSpriteEditorPanel::OnionSkinsPanelId(TEXT("Paper2DPlus.Sprite.OnionSkins"));
const FName SSpriteEditorPanel::BatchPanelId(TEXT("Paper2DPlus.Sprite.Batch"));

// ==========================================
// Construct / Destruct
// ==========================================

void SSpriteEditorPanel::Construct(const FArguments& InArgs)
{
	Model = InArgs._Model;
	HostContract = InArgs._HostContract.IsValid()
		? InArgs._HostContract
		: FProfileToolPanelHostContract::Embedded();
	if (Model.IsValid())
	{
		Asset = Model->GetAsset();

		ModelFlipbookSelectionHandle = Model->OnFlipbookSelectionChanged.AddSP(this, &SSpriteEditorPanel::OnFlipbookSelected);
		ModelFrameSelectionHandle = Model->OnFrameSelectionChanged.AddSP(
			this, &SSpriteEditorPanel::HandleModelFrameSelectionChanged);
		ModelGroupCollapseHandle = Model->OnGroupCollapseChanged.AddLambda([this]() {
		});
		ModelSearchTextHandle = Model->OnSearchTextChanged.AddLambda([this](const FString&) {
		});
		ModelAssetDataChangedHandle = Model->OnAssetDataChanged.AddLambda([this]() {
			RefreshAll();
		});
		ModelAssetExternallyModifiedHandle = Model->OnAssetExternallyModified.AddLambda([this]() {
			if (ActiveTransaction.IsValid()) return;
			// Undo/reinstance or another editor may have replaced the frame arrays. End any stale
			// pointer gesture and discard numeric multi-selection indices before reading the new data.
			FinishActiveEditGesture(/*bReleaseCanvasCapture=*/true);
			ClearFrameSelection();
			RefreshAll();
		});
		ModelDirectionalPreviewHandle = Model->OnDirectionalPreviewChanged.AddLambda([this]()
		{
			const ECharacterProfileDirectionalPreviewState PreviewState =
				Model->GetDirectionalPreview().State;
			using namespace Paper2DPlusEditor::DirectionalPreviewPlayback;
			EEvent PlaybackEvent = EEvent::BecameUnavailable;
			if (PreviewState == ECharacterProfileDirectionalPreviewState::Resolving)
			{
				PlaybackEvent = EEvent::BeginResolving;
			}
			else if (PreviewState == ECharacterProfileDirectionalPreviewState::Empty)
			{
				PlaybackEvent = EEvent::BecameEmpty;
			}
			else if (Model->IsDirectionalPreviewRenderable())
			{
				PlaybackEvent = EEvent::BecameRenderable;
			}
			const FDecision Decision = Resolve(
				bIsPlaying,
				bResumeAfterDirectionalPreviewResolves,
				PlaybackEvent);
			if (PreviewState == ECharacterProfileDirectionalPreviewState::Resolving
				|| PreviewState == ECharacterProfileDirectionalPreviewState::Empty)
			{
				StopPlayback();
				bResumeAfterDirectionalPreviewResolves =
					Decision.bResumeAfterDirectionalPreviewResolves;
			}
			else if (Model->IsDirectionalPreviewRenderable())
			{
				bResumeAfterDirectionalPreviewResolves =
					Decision.bResumeAfterDirectionalPreviewResolves;
				if (Decision.bShouldBePlaying && !bIsPlaying)
				{
					StartPlayback();
				}
			}
			else
			{
				StopPlayback();
			}
			RefreshSpriteEditorFrameList();
			InvalidateSpriteEditorCanvas();
		});

		// When queue playback starts externally (on FlipbookListPanel), stop local single-flipbook playback
		ModelQueuePlaybackStateHandle = Model->OnQueuePlaybackStateChanged.AddLambda([this](bool bPlaying)
		{
			if (bPlaying)
			{
				FinishActiveEditGesture(/*bReleaseCanvasCapture=*/true);
				if (bIsPlaying) StopPlayback();
			}
		});
	}

	const TSharedRef<SWidget> CentralWorkspace = BuildCentralWorkspace();
	if (HostContract.UsesExternalNavigation())
	{
		ChildSlot
		[
			CentralWorkspace
		];
	}
	else
	{
		ChildSlot
		[
			SNew(SSplitter)
			.Orientation(Orient_Horizontal)
			+ SSplitter::Slot()
			.Value(0.75f)
			[
				CentralWorkspace
			]
			+ SSplitter::Slot()
			.Value(0.25f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					BuildEmbeddedContextStack()
				]
			]
		];
	}

	// Register for undo/redo
	if (GEditor)
	{
		GEditor->RegisterForUndo(this);
	}
}

SSpriteEditorPanel::~SSpriteEditorPanel()
{
	StopPlayback();
	if (TSharedPtr<FActiveTimerHandle> Timer = NudgeDebounceTimer.Pin())
	{
		UnRegisterActiveTimer(Timer.ToSharedRef());
	}
	NudgeDebounceTimer.Reset();
	EndTransaction();

	if (Model.IsValid())
	{
		if (ModelFlipbookSelectionHandle.IsValid()) Model->OnFlipbookSelectionChanged.Remove(ModelFlipbookSelectionHandle);
		if (ModelFrameSelectionHandle.IsValid()) Model->OnFrameSelectionChanged.Remove(ModelFrameSelectionHandle);
		if (ModelGroupCollapseHandle.IsValid()) Model->OnGroupCollapseChanged.Remove(ModelGroupCollapseHandle);
		if (ModelSearchTextHandle.IsValid()) Model->OnSearchTextChanged.Remove(ModelSearchTextHandle);
		if (ModelAssetDataChangedHandle.IsValid()) Model->OnAssetDataChanged.Remove(ModelAssetDataChangedHandle);
		if (ModelAssetExternallyModifiedHandle.IsValid()) Model->OnAssetExternallyModified.Remove(ModelAssetExternallyModifiedHandle);
		if (ModelDirectionalPreviewHandle.IsValid()) Model->OnDirectionalPreviewChanged.Remove(ModelDirectionalPreviewHandle);
		if (ModelQueuePlaybackStateHandle.IsValid()) Model->OnQueuePlaybackStateChanged.Remove(ModelQueuePlaybackStateHandle);
	}

	if (GEditor)
	{
		GEditor->UnregisterForUndo(this);
	}
}

void SSpriteEditorPanel::GetContextualPanels(
	TArray<FProfileToolPanelDescriptor>& OutPanels) const
{
	if (!HostContract.UsesExternalNavigation())
	{
		return;
	}

	const TWeakPtr<SSpriteEditorPanel> WeakController =
		ConstCastSharedRef<SSpriteEditorPanel>(SharedThis(this));
	auto AddPanel = [&OutPanels, WeakController](
		FName PanelId,
		const FText& Label,
		const FText& ToolTip,
		TFunction<TSharedRef<SWidget>(SSpriteEditorPanel&)> Builder)
	{
		FProfileToolPanelDescriptor Descriptor;
		Descriptor.PanelId = PanelId;
		Descriptor.Label = Label;
		Descriptor.ToolTip = ToolTip;
		Descriptor.CapabilityId = PanelId;
		Descriptor.IsAvailable = [WeakController]() { return WeakController.IsValid(); };
		Descriptor.WidgetFactory = [WeakController, PanelId, Builder = MoveTemp(Builder)]() -> TSharedRef<SWidget>
		{
			const TSharedPtr<SSpriteEditorPanel> Controller = WeakController.Pin();
			if (!Controller.IsValid())
			{
				return SNullWidget::NullWidget;
			}
			++Controller->ContextPanelBuildCounts.FindOrAdd(PanelId);
			Controller->ContextPanelResolvedFrames.FindOrAdd(PanelId) = Controller->Model.IsValid()
				? Controller->Model->GetSelectedFrameIndex()
				: INDEX_NONE;
			return Builder(*Controller);
		};
		OutPanels.Add(MoveTemp(Descriptor));
	};

	AddPanel(
		OffsetNudgePanelId,
		LOCTEXT("SpriteOffsetNudgeContext", "Offset & Nudge"),
		LOCTEXT("SpriteOffsetNudgeContextTip", "Edit the current frame's authored offset or nudge selected frames."),
		[](SSpriteEditorPanel& Controller) { return Controller.BuildOffsetNudgePanel(); });
	AddPanel(
		OnionSkinsPanelId,
		LOCTEXT("SpriteOnionSkinsContext", "Onion & Skins"),
		LOCTEXT("SpriteOnionSkinsContextTip", "Compare neighboring frames and a chosen reference sprite in the central preview."),
		[](SSpriteEditorPanel& Controller) { return Controller.BuildOnionSkinsPanel(); });
	AddPanel(
		BatchPanelId,
		LOCTEXT("SpriteBatchContext", "Batch"),
		LOCTEXT("SpriteBatchContextTip", "Apply, reset, mirror, or set offsets across a chosen frame range."),
		[](SSpriteEditorPanel& Controller) { return Controller.BuildBatchPanel(); });
}

void SSpriteEditorPanel::HandleHostDeactivated()
{
	StopPlayback();
	FinishActiveEditGesture(/*bReleaseCanvasCapture=*/true);
}

FIntPoint SSpriteEditorPanel::GetCurrentOffsetForTests() const
{
	const FFlipbookProfileEntry* Animation = GetCurrentFlipbookData();
	const int32 FrameIndex = Model.IsValid() ? Model->GetSelectedFrameIndex() : INDEX_NONE;
	return Animation && Animation->CombatData.FrameExtractionInfo.IsValidIndex(FrameIndex)
		? Animation->CombatData.FrameExtractionInfo[FrameIndex].SpriteOffset
		: FIntPoint::ZeroValue;
}

void SSpriteEditorPanel::CommitPendingEditForTests()
{
	FinishActiveEditGesture();
}

void SSpriteEditorPanel::ConfigureBatchForTests(
	int32 ActionIndex,
	int32 TargetIndex,
	FIntPoint CustomValue)
{
	AlignBatchActionIndex = ActionIndex;
	AlignBatchTargetIndex = TargetIndex;
	AlignBatchCustomValue = CustomValue;
}

void SSpriteEditorPanel::SetOnionSkinsStateForTests(
	bool bInOnion,
	bool bInForwardOnion,
	int32 InOnionFrames,
	float InOnionOpacity,
	int32 InReferenceFlipbook,
	int32 InReferenceFrame,
	float InReferenceOpacity)
{
	bShowOnionSkin = bInOnion;
	bShowForwardOnionSkin = bInForwardOnion;
	OnionSkinFrames = FMath::Clamp(InOnionFrames, 1, 3);
	OnionSkinOpacity = FMath::Clamp(InOnionOpacity, 0.1f, 0.8f);
	ReferenceFlipbookIndex = InReferenceFlipbook;
	ReferenceFrameIndex = InReferenceFrame;
	ReferenceSpriteOpacity = FMath::Clamp(InReferenceOpacity, 0.1f, 0.8f);
	bShowReferenceSprite = InReferenceFlipbook != INDEX_NONE && InReferenceFrame != INDEX_NONE;
	InvalidateSpriteEditorCanvas();
}

bool SSpriteEditorPanel::IsCanvasShowingOnionForTests() const
{
	return SpriteEditorCanvas.IsValid() && SpriteEditorCanvas->IsShowingOnionSkinForTests();
}

bool SSpriteEditorPanel::IsCanvasShowingReferenceForTests() const
{
	return SpriteEditorCanvas.IsValid() && SpriteEditorCanvas->IsShowingReferenceSpriteForTests();
}

bool SSpriteEditorPanel::IsShortcutProtectedWidgetTypeForTests(const FString& WidgetTypeName)
{
	return Paper2DPlusEditor::SlateShortcutUtils::IsInputWidgetTypeName(WidgetTypeName);
}

// ==========================================
// Undo/Redo
// ==========================================

void SSpriteEditorPanel::PostUndo(bool bSuccess)
{
	if (bSuccess)
	{
		FinishActiveEditGesture(/*bReleaseCanvasCapture=*/true);
		ClearFrameSelection();
		RefreshAll();
	}
}

void SSpriteEditorPanel::PostRedo(bool bSuccess)
{
	if (bSuccess)
	{
		FinishActiveEditGesture(/*bReleaseCanvasCapture=*/true);
		ClearFrameSelection();
		RefreshAll();
	}
}

void SSpriteEditorPanel::BeginTransaction(const FText& Description)
{
	if (!ActiveTransaction.IsValid() && Asset.IsValid())
	{
		ActiveTransaction = MakeUnique<FScopedTransaction>(Description);
		Asset->SetFlags(RF_Transactional);
		Asset->Modify();
		++TransactionBeginCount;
	}
}

void SSpriteEditorPanel::EndTransaction()
{
	if (ActiveTransaction.IsValid())
	{
		if (Asset.IsValid())
		{
			Asset->MarkPackageDirty();
		}
		ActiveTransaction.Reset();
		++TransactionEndCount;
	}
}

void SSpriteEditorPanel::FinishActiveEditGesture(bool bReleaseCanvasCapture)
{
	if (bReleaseCanvasCapture
		&& FSlateApplication::IsInitialized()
		&& SpriteEditorCanvas.IsValid()
		&& SpriteEditorCanvas->HasMouseCapture())
	{
		// Capture loss invokes OnDragEnded; the remaining work below is the idempotent fallback.
		FSlateApplication::Get().ReleaseAllPointerCapture();
	}
	if (TSharedPtr<FActiveTimerHandle> Timer = NudgeDebounceTimer.Pin())
	{
		UnRegisterActiveTimer(Timer.ToSharedRef());
	}
	NudgeDebounceTimer.Reset();
	bSpriteEditorDragActive = false;
	EndTransaction();
}

void SSpriteEditorPanel::HandleModelFrameSelectionChanged()
{
	// A picker/timeline change may arrive while the nudge debounce or canvas drag owns a transaction.
	// Finish it before another input can mutate the newly selected frame under the old gesture.
	FinishActiveEditGesture(/*bReleaseCanvasCapture=*/true);
	RefreshSpriteEditorFrameList();
	RefreshCurrentFrameFlipState();
}

void SSpriteEditorPanel::OnFlipbookSelected(int32 Index)
{
	FinishActiveEditGesture(/*bReleaseCanvasCapture=*/true);
	if (Model.IsValid())
	{
		Model->SetSelectedFlipbook(Index);
	}
	RefreshCurrentFrameFlipState();
	RefreshSpriteEditorFrameList();
}

void SSpriteEditorPanel::OnFrameSelected(int32 Index)
{
	FinishActiveEditGesture(/*bReleaseCanvasCapture=*/true);
	if (Model.IsValid())
	{
		Model->SetSelectedFrame(Index);
	}
	RefreshCurrentFrameFlipState();
}

// ==========================================
// Helpers
// ==========================================

const FFlipbookProfileEntry* SSpriteEditorPanel::GetCurrentFlipbookData() const
{
	if (!Asset.IsValid() || !Model.IsValid()) return nullptr;
	int32 Idx = Model->GetSelectedFlipbookIndex();
	if (!Asset->Flipbooks.IsValidIndex(Idx)) return nullptr;
	return &Asset->Flipbooks[Idx];
}

FFlipbookProfileEntry* SSpriteEditorPanel::GetCurrentFlipbookDataMutable()
{
	if (!Asset.IsValid() || !Model.IsValid()) return nullptr;
	int32 Idx = Model->GetSelectedFlipbookIndex();
	if (!Asset->Flipbooks.IsValidIndex(Idx)) return nullptr;
	return &Asset->Flipbooks[Idx];
}

int32 SSpriteEditorPanel::GetCurrentFrameCount() const
{
	const FFlipbookProfileEntry* Anim = GetCurrentFlipbookData();
	if (!Anim) return 0;
	if (!Anim->Identity.Flipbook.IsNull())
	{
		UPaperFlipbook* FB = Anim->Identity.Flipbook.LoadSynchronous();
		if (FB) return FB->GetNumKeyFrames();
	}
	return Anim->CombatData.Frames.Num();
}

UPaperSprite* SSpriteEditorPanel::GetCurrentSprite() const
{
	UPaperFlipbook* FB = GetPreviewFlipbook();
	int32 FrameIdx = Model.IsValid() ? Model->GetSelectedFrameIndex() : 0;
	if (!FB || FrameIdx < 0 || FrameIdx >= FB->GetNumKeyFrames()) return nullptr;
	return FB->GetKeyFrameChecked(FrameIdx).Sprite;
}

UPaperFlipbook* SSpriteEditorPanel::GetPreviewFlipbook() const
{
	if (Model.IsValid() && Model->IsDirectionalPreviewEnabled())
	{
		return Model->GetDirectionalPreviewFlipbook();
	}
	const FFlipbookProfileEntry* Anim = GetCurrentFlipbookData();
	return Anim && !Anim->Identity.Flipbook.IsNull()
		? Anim->Identity.Flipbook.LoadSynchronous()
		: nullptr;
}

void SSpriteEditorPanel::ClearFrameSelection()
{
	if (Model.IsValid())
	{
		Model->ClearFrameSelection();
	}
}

// ==========================================
// Refresh
// ==========================================

void SSpriteEditorPanel::RefreshAll()
{
	RefreshSpriteEditorFrameList();
	RefreshCurrentFrameFlipState();
}

// ==========================================
// Section layout persistence
// ==========================================

// ==========================================
// Keyboard handling
// ==========================================

FReply SSpriteEditorPanel::OnPreviewKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	FKey Key = InKeyEvent.GetKey();

	// Let editable text fields receive arrow keys
	if (Paper2DPlusEditor::SlateShortcutUtils::ShouldIgnoreShortcutForFocusedWidget())
	{
		return FReply::Unhandled();
	}

	const int32 SelectedFlipbookIndex = Model.IsValid() ? Model->GetSelectedFlipbookIndex() : 0;
	const int32 SelectedFrameIndex = Model.IsValid() ? Model->GetSelectedFrameIndex() : 0;
	const bool bQueueActive = Model.IsValid() && Model->IsQueueActive();

	if (Key == EKeys::Left || Key == EKeys::Comma)
	{
		if (SelectedFrameIndex > 0)
		{
			OnFrameSelected(SelectedFrameIndex - 1);
		}
		else if (Model.IsValid())
		{
			// StepQueue reports INDEX_NONE whenever the queue cannot supply a previous animation
			// (empty/single-entry queue, or the selection is off the queue) — fall back to wrapping
			// inside the current flipbook.
			if (Model->StepQueue(-1, /*bLandOnLastFrame=*/true) == INDEX_NONE)
			{
				int32 FrameCount = GetCurrentFrameCount();
				if (FrameCount > 1) OnFrameSelected(FrameCount - 1);
			}
		}
		RefreshSpriteEditorFrameList();
		return FReply::Handled();
	}

	if (Key == EKeys::Right || Key == EKeys::Period)
	{
		int32 FrameCount = GetCurrentFrameCount();
		if (SelectedFrameIndex < FrameCount - 1)
		{
			OnFrameSelected(SelectedFrameIndex + 1);
		}
		else if (Model.IsValid())
		{
			if (Model->StepQueue(1) == INDEX_NONE)
			{
				if (FrameCount > 1) OnFrameSelected(0);
			}
		}
		RefreshSpriteEditorFrameList();
		return FReply::Handled();
	}

	if (Key == EKeys::Up || Key == EKeys::Down)
	{
		const int32 Direction = (Key == EKeys::Up) ? -1 : 1;
		// Only the fallback path needs an explicit refresh: StepQueue drives the model, whose
		// selection broadcast already rebuilds the strip. Refreshing on a no-op key press would
		// rebuild the frame strip for nothing.
		if (!bQueueActive || Model->StepQueue(Direction) == INDEX_NONE)
		{
			const int32 NewIdx = Model.IsValid()
				? Model->GetVisualAdjacentFlipbookIndex(Direction) : INDEX_NONE;
			if (NewIdx != INDEX_NONE && NewIdx != SelectedFlipbookIndex)
			{
				OnFlipbookSelected(NewIdx);
				RefreshSpriteEditorFrameList();
			}
		}
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

FReply SSpriteEditorPanel::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (Paper2DPlusEditor::SlateShortcutUtils::ShouldIgnoreShortcutForFocusedWidget())
	{
		return FReply::Unhandled();
	}

	const int32 SelectedFlipbookIndex = Model.IsValid() ? Model->GetSelectedFlipbookIndex() : 0;
	const int32 SelectedFrameIndex = Model.IsValid() ? Model->GetSelectedFrameIndex() : 0;

	// Ctrl+Z / Ctrl+Y — undo/redo
	if (GEditor && InKeyEvent.IsControlDown() && !InKeyEvent.IsShiftDown() && InKeyEvent.GetKey() == EKeys::Z)
	{
		FinishActiveEditGesture(/*bReleaseCanvasCapture=*/true);
		GEditor->UndoTransaction();
		return FReply::Handled();
	}
	if (GEditor && InKeyEvent.IsControlDown() && (InKeyEvent.GetKey() == EKeys::Y || (InKeyEvent.IsShiftDown() && InKeyEvent.GetKey() == EKeys::Z)))
	{
		FinishActiveEditGesture(/*bReleaseCanvasCapture=*/true);
		GEditor->RedoTransaction();
		return FReply::Handled();
	}
	if (Paper2DPlusEditor::SlateShortcutUtils::HasEditorCommandModifier(InKeyEvent))
	{
		return FReply::Unhandled();
	}

	// Space — toggle queue playback if queue active, else single-flipbook playback
	if (InKeyEvent.GetKey() == EKeys::SpaceBar)
	{
		if (Model.IsValid() && Model->IsQueueActive())
		{
			Model->SetQueuePlaying(!Model->IsQueuePlaying());
		}
		else
		{
			TogglePlayback();
		}
		return FReply::Handled();
	}

	// WASD — nudge offsets. Shift keeps its authored coarse-nudge meaning.
	{
		const int32 Multiplier = InKeyEvent.IsShiftDown() ? 10 : 1;

		if (InKeyEvent.GetKey() == EKeys::W) { NudgeOffset(0, -1 * Multiplier); return FReply::Handled(); }
		if (InKeyEvent.GetKey() == EKeys::A) { NudgeOffset(-1 * Multiplier, 0); return FReply::Handled(); }
		if (InKeyEvent.GetKey() == EKeys::S) { NudgeOffset(0, 1 * Multiplier); return FReply::Handled(); }
		if (InKeyEvent.GetKey() == EKeys::D) { NudgeOffset(1 * Multiplier, 0); return FReply::Handled(); }
	}

	// O — toggle onion skin
	if (InKeyEvent.GetKey() == EKeys::O)
	{
		bShowOnionSkin = !bShowOnionSkin;
		InvalidateSpriteEditorCanvas();
		return FReply::Handled();
	}

	// F — toggle forward onion skin
	if (InKeyEvent.GetKey() == EKeys::F)
	{
		bShowForwardOnionSkin = !bShowForwardOnionSkin;
		InvalidateSpriteEditorCanvas();
		return FReply::Handled();
	}

	// P — toggle ping-pong
	if (InKeyEvent.GetKey() == EKeys::P) { bPingPongPlayback = !bPingPongPlayback; if (!bPingPongPlayback) bPlaybackReversed = false; return FReply::Handled(); }

	// R — toggle reference sprite
	if (InKeyEvent.GetKey() == EKeys::R)
	{
		if (ReferenceFlipbookIndex == INDEX_NONE)
		{
			SetReferenceSprite(SelectedFlipbookIndex, SelectedFrameIndex);
		}
		else
		{
			bShowReferenceSprite = !bShowReferenceSprite;
		}
		InvalidateSpriteEditorCanvas();
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

// ==========================================
// UI Builders
// ==========================================

TSharedRef<SWidget> SSpriteEditorPanel::BuildCentralWorkspace()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			BuildSpriteEditorToolbar()
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4, 2, 4, 0)
		[
			SNew(STextBlock)
			.Text_Lambda([this]()
			{
				const FFlipbookProfileEntry* Animation = GetCurrentFlipbookData();
				if (!Animation)
				{
					return LOCTEXT("NoSpriteAnimation", "No animation selected");
				}
				// Same directional-empty explanation the Frame Timing / Frame Cues / Root Motion
				// titles carry: never render an unexplained blank canvas.
				if (!GetPreviewFlipbook() && Model.IsValid()
					&& Model->IsDirectionalPreviewEnabled())
				{
					return FText::FromString(Model->GetDirectionalPreview().Reason);
				}
				const int32 FrameCount = GetCurrentFrameCount();
				const int32 SelectedFrame = Model.IsValid() ? Model->GetSelectedFrameIndex() : INDEX_NONE;
				return FText::Format(
					LOCTEXT("SpriteFlipbookTitleFmt", "{0}  Frame {1}/{2}"),
					FText::FromString(Animation->Identity.FlipbookName),
					FText::AsNumber(SelectedFrame + 1),
					FText::AsNumber(FrameCount));
			})
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			BuildSpriteEditorCanvasArea()
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			BuildSpriteEditorFrameList()
		];
}

TSharedRef<SWidget> SSpriteEditorPanel::BuildEmbeddedContextStack()
{
	// Embedded mode keeps the established self-contained layout. Onion/reference remains in the
	// toolbar's Skins popover there; external Character mode exposes it as a direct Details section.
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(4)
		[
			BuildOffsetNudgePanel()
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(4)
		[
			BuildBatchPanel()
		];
}

TSharedRef<SWidget> SSpriteEditorPanel::BuildSpriteEditorToolbar()
{
	if (PlaybackModeOptions.Num() == 0)
	{
		PlaybackModeOptions.Add(MakeShared<FString>(TEXT("Forward")));
		PlaybackModeOptions.Add(MakeShared<FString>(TEXT("Ping-Pong")));
		PlaybackModeOptions.Add(MakeShared<FString>(TEXT("Reverse")));
	}

	int32 InitialModeIdx = bPingPongPlayback ? 1 : (bPlaybackReversed ? 2 : 0);

	return SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
		.Padding(FMargin(8, 4))
		[
			SNew(SHorizontalBox)

			// === Playback Mode ===
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 4, 0)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("PlaybackLabel", "Playback:"))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 0, 8, 0)
			[
				SNew(SComboBox<TSharedPtr<FString>>)
				.OptionsSource(&PlaybackModeOptions)
				.InitiallySelectedItem(PlaybackModeOptions[InitialModeIdx])
				.OnSelectionChanged_Lambda([this](TSharedPtr<FString> Item, ESelectInfo::Type)
				{
					if (!Item.IsValid()) return;
					const int32 Idx = PlaybackModeOptions.IndexOfByKey(Item);
					bPingPongPlayback = (Idx == 1);
					bPlaybackReversed = (Idx == 2);
				})
				.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item) -> TSharedRef<SWidget>
				{
					return SNew(STextBlock).Text(FText::FromString(*Item)).Font(FCoreStyle::GetDefaultFontStyle("Regular", 8));
				})
				[
					SNew(STextBlock)
					.Text_Lambda([this]() -> FText
					{
						if (bPingPongPlayback) return LOCTEXT("ModePingPong", "Ping-Pong");
						if (bPlaybackReversed) return LOCTEXT("ModeReverse", "Reverse");
						return LOCTEXT("ModeForward", "Forward");
					})
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
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

			// === Skins Mixer ===
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 8, 0)
			[
				SNew(SComboButton)
				.Visibility(HostContract.OwnsEmbeddedNavigation() ? EVisibility::Visible : EVisibility::Collapsed)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.ContentPadding(FMargin(4, 2))
				.HasDownArrow(true)
				.ButtonContent()
				[
					SNew(STextBlock)
					.Text_Lambda([this]() -> FText
					{
						int32 ActiveCount = (bShowOnionSkin ? 1 : 0) + (bShowForwardOnionSkin ? 1 : 0)
							+ ((bShowReferenceSprite && ReferenceFlipbookIndex != INDEX_NONE) ? 1 : 0);
						if (ActiveCount > 0)
							return FText::Format(LOCTEXT("SkinsActive", "Skins ({0})"), FText::AsNumber(ActiveCount));
						return LOCTEXT("SkinsNone", "Skins");
					})
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				]
				.MenuContent()
				[
					BuildOnionSkinsPanel()
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

			// === Reticle ===
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 4, 0)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]() { return bShowSpriteEditorReticle ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState) { bShowSpriteEditorReticle = (NewState == ECheckBoxState::Checked); })
				.ToolTipText(LOCTEXT("ReticleToggleTooltip", "Show alignment reticle. Alt+left-drag on canvas to reposition."))
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ReticleLabel", "Reticle"))
				]
			]

			// Spacer
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNullWidget::NullWidget
			]
		];
}

TSharedRef<SWidget> SSpriteEditorPanel::BuildSpriteEditorFrameList()
{
	SAssignNew(SpriteEditorFrameListBox, SHorizontalBox);

	return SNew(SVerticalBox)

		// Excluded count + restore
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			.Visibility_Lambda([this]() -> EVisibility
			{
				const int32 SelFB = Model.IsValid() ? Model->GetSelectedFlipbookIndex() : 0;
				return (Asset.IsValid() && Asset->GetExcludedFlipbookFrameCount(SelFB) > 0)
					? EVisibility::Visible : EVisibility::Collapsed;
			})
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text_Lambda([this]() {
					const int32 SelFB = Model.IsValid() ? Model->GetSelectedFlipbookIndex() : 0;
					const int32 ExcludedCount = Asset.IsValid() ? Asset->GetExcludedFlipbookFrameCount(SelFB) : 0;
					return FText::Format(LOCTEXT("ExcludedCountLabel", "Excluded: {0}"), FText::AsNumber(ExcludedCount));
				})
				.ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f)))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SComboButton)
				.ToolTipText(LOCTEXT("RestoreExcludedSpriteEditorTooltip", "Restore excluded frames back into this flipbook"))
				.OnGetMenuContent_Lambda([this]() { return BuildSpriteEditorRestoreExcludedMenu(); })
				.ButtonContent()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("RestoreExcludedFramesShort", "Restore"))
				]
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.MaxHeight(70.0f)
		[
			SNew(SScrollBox)
			.Orientation(Orient_Horizontal)
			+ SScrollBox::Slot()
			[
				SpriteEditorFrameListBox.ToSharedRef()
			]
		];
}

TSharedRef<SWidget> SSpriteEditorPanel::BuildSpriteEditorCanvasArea()
{
	const int32 SelectedFlipbookIndex = Model.IsValid() ? Model->GetSelectedFlipbookIndex() : 0;
	const int32 SelectedFrameIndex = Model.IsValid() ? Model->GetSelectedFrameIndex() : 0;

	TSharedRef<SBorder> Border = SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
		.Padding(0)
		[
			SAssignNew(SpriteEditorCanvas, SSpriteEditorCanvas)
			.Asset(Asset.Get())
			.PreviewFlipbook_Lambda([this]() { return GetPreviewFlipbook(); })
			.SelectedFlipbookIndex_Lambda([this]() { return Model.IsValid() ? Model->GetSelectedFlipbookIndex() : 0; })
			.SelectedFrameIndex_Lambda([this]() { return Model.IsValid() ? Model->GetSelectedFrameIndex() : 0; })
			.Zoom_Lambda([this]() { return SpriteEditorZoomLevel; })
			.ShowOnionSkin_Lambda([this]() { return bShowOnionSkin; })
			.OnionSkinFrames_Lambda([this]() { return OnionSkinFrames; })
			.ForwardOnionSkinFrames_Lambda([this]() { return ForwardOnionSkinFrames; })
			.OnionSkinOpacity_Lambda([this]() { return OnionSkinOpacity; })
			.PreviousFlipbookIndex_Lambda([this]() { return Model.IsValid() ? Model->GetQueueAdjacentFlipbookIndex(-1) : INDEX_NONE; })
			.ShowForwardOnionSkin_Lambda([this]() { return bShowForwardOnionSkin; })
			.NextFlipbookIndex_Lambda([this]() { return Model.IsValid() ? Model->GetQueueAdjacentFlipbookIndex(1) : INDEX_NONE; })
			.ShowReticle_Lambda([this]() { return bShowSpriteEditorReticle; })
			.ReticlePosition_Lambda([this]() { return SpriteEditorReticlePos; })
			.FlipX_Lambda([this]() { return bSpriteFlipX; })
			.FlipY_Lambda([this]() { return bSpriteFlipY; })
			.ShowReferenceSprite_Lambda([this]() { return bShowReferenceSprite && ReferenceFlipbookIndex != INDEX_NONE; })
			.ReferenceSprite_Lambda([this]() -> TWeakObjectPtr<UPaperSprite> {
				if (!bShowReferenceSprite || !Asset.IsValid()) return nullptr;
				if (!Asset->Flipbooks.IsValidIndex(ReferenceFlipbookIndex)) return nullptr;
				const FFlipbookProfileEntry& Anim = Asset->Flipbooks[ReferenceFlipbookIndex];
				if (Anim.Identity.Flipbook.IsNull()) return nullptr;
				UPaperFlipbook* FB = Anim.Identity.Flipbook.LoadSynchronous();
				if (!FB || ReferenceFrameIndex < 0 || ReferenceFrameIndex >= FB->GetNumKeyFrames()) return nullptr;
				return FB->GetKeyFrameChecked(ReferenceFrameIndex).Sprite;
			})
			.ReferenceSpriteOffset_Lambda([this]() -> FIntPoint {
				if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(ReferenceFlipbookIndex)) return FIntPoint::ZeroValue;
				const FFlipbookProfileEntry& Anim = Asset->Flipbooks[ReferenceFlipbookIndex];
				if (!Anim.CombatData.FrameExtractionInfo.IsValidIndex(ReferenceFrameIndex)) return FIntPoint::ZeroValue;
				return Anim.CombatData.FrameExtractionInfo[ReferenceFrameIndex].SpriteOffset;
			})
			.ReferenceSpriteOpacity_Lambda([this]() { return ReferenceSpriteOpacity; })
			.QueueLargestDims_Lambda([this]() -> FIntPoint {
				if (!Model.IsValid() || !Asset.IsValid()) return FIntPoint::ZeroValue;
				const TArray<int32>& Q = Model->GetPlaybackQueue();
				if (Q.Num() < 2) return FIntPoint::ZeroValue;
				FIntPoint Largest(1, 1);
				for (int32 Idx : Q)
				{
					if (!Asset->Flipbooks.IsValidIndex(Idx)) continue;
					const FFlipbookProfileEntry& FBData = Asset->Flipbooks[Idx];
					if (FBData.Identity.Flipbook.IsNull()) continue;
					UPaperFlipbook* FB = FBData.Identity.Flipbook.Get();
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
			.ExcludedPreviewSprite_Lambda([this]() -> TWeakObjectPtr<UPaperSprite> {
				const int32 SelExcl = Model.IsValid() ? Model->GetSelectedExcludedFrameIndex() : INDEX_NONE;
				const int32 SelFB = Model.IsValid() ? Model->GetSelectedFlipbookIndex() : 0;
				if (SelExcl == INDEX_NONE || !Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(SelFB)) return nullptr;
				const auto& Excluded = Asset->Flipbooks[SelFB].CombatData.ExcludedFrames;
				if (!Excluded.IsValidIndex(SelExcl)) return nullptr;
				return Excluded[SelExcl].KeyFrame.Sprite;
			})
			.ExcludedPreviewOffset_Lambda([this]() -> FIntPoint {
				const int32 SelExcl = Model.IsValid() ? Model->GetSelectedExcludedFrameIndex() : INDEX_NONE;
				const int32 SelFB = Model.IsValid() ? Model->GetSelectedFlipbookIndex() : 0;
				if (SelExcl == INDEX_NONE || !Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(SelFB)) return FIntPoint::ZeroValue;
				const auto& Excluded = Asset->Flipbooks[SelFB].CombatData.ExcludedFrames;
				if (!Excluded.IsValidIndex(SelExcl)) return FIntPoint::ZeroValue;
				return Excluded[SelExcl].ExtractionInfo.SpriteOffset;
			})
		];

	if (SpriteEditorCanvas.IsValid())
	{
		SpriteEditorCanvas->OnOffsetChanged.BindSP(this, &SSpriteEditorPanel::OnSpriteEditorOffsetChanged);
		SpriteEditorCanvas->OnReticlePositionChanged.BindLambda([this](FVector2D NewPos) {
			SpriteEditorReticlePos = NewPos;
		});
		SpriteEditorCanvas->OnZoomChanged.BindLambda([this](float NewZoom) {
			SpriteEditorZoomLevel = NewZoom;
		});
		SpriteEditorCanvas->OnDragStarted.BindLambda([this]() {
			bSpriteEditorDragActive = true;
		});
		SpriteEditorCanvas->OnDragEnded.BindLambda([this]() {
			if (bSpriteEditorDragActive)
			{
				FinishActiveEditGesture();
			}
		});
	}

	return Border;
}

TSharedRef<SWidget> SSpriteEditorPanel::BuildOffsetNudgePanel()
{
	TSharedPtr<SVerticalBox> ControlsBox;
	SAssignNew(ControlsBox, SVerticalBox)

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
			.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.DarkGroupBorder"))
			.Padding(8)
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 0, 0, 4)
				[
					SNew(SHorizontalBox)
					.ToolTipText(LOCTEXT("OffsetXTooltip", "Horizontal offset in pixels."))
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
					[
						SNew(STextBlock).Text(LOCTEXT("OffsetX", "X:"))
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f)
					[
						SNew(SSpinBox<int32>).MinValue(-500).MaxValue(500)
						.Value_Lambda([this]() {
							const FFlipbookProfileEntry* Anim = GetCurrentFlipbookData();
							const int32 SelFrame = Model.IsValid() ? Model->GetSelectedFrameIndex() : 0;
							if (Anim && Anim->CombatData.FrameExtractionInfo.IsValidIndex(SelFrame))
								return Anim->CombatData.FrameExtractionInfo[SelFrame].SpriteOffset.X;
							return 0;
						})
						.OnValueChanged_Lambda([this](int32 NewValue) { OnOffsetXChanged(NewValue); })
					]
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 0, 0, 4)
				[
					SNew(SHorizontalBox)
					.ToolTipText(LOCTEXT("OffsetYTooltip", "Vertical offset in pixels."))
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
					[
						SNew(STextBlock).Text(LOCTEXT("OffsetY", "Y:"))
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f)
					[
						SNew(SSpinBox<int32>).MinValue(-500).MaxValue(500)
						.Value_Lambda([this]() {
							const FFlipbookProfileEntry* Anim = GetCurrentFlipbookData();
							const int32 SelFrame = Model.IsValid() ? Model->GetSelectedFrameIndex() : 0;
							if (Anim && Anim->CombatData.FrameExtractionInfo.IsValidIndex(SelFrame))
								return Anim->CombatData.FrameExtractionInfo[SelFrame].SpriteOffset.Y;
							return 0;
						})
						.OnValueChanged_Lambda([this](int32 NewValue) { OnOffsetYChanged(NewValue); })
					]
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
					.HAlign(HAlign_Center)
					.ToolTipText(LOCTEXT("ResetOffsetTooltip", "Reset offset to zero"))
					.OnClicked_Lambda([this]() { OnResetOffset(); return FReply::Handled(); })
					[
						SNew(STextBlock).Text(LOCTEXT("ResetOffset", "Reset Offset"))
					]
				]

				// Copy/Paste act on this same offset, so they sit with it under Reset rather than
				// in a section of their own (the retired CLIPBOARD category).
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 2, 0, 0)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 2, 0)
					[
						SNew(SButton).ButtonStyle(FAppStyle::Get(), "FlatButton.Default").HAlign(HAlign_Center)
						.ToolTipText(LOCTEXT("CopyOffsetTooltip", "Copy this frame's offset."))
						.OnClicked_Lambda([this]() { OnCopyOffset(); return FReply::Handled(); })
						[ SNew(STextBlock).Text(LOCTEXT("Copy", "Copy")) ]
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2, 0, 0, 0)
					[
						SNew(SButton).ButtonStyle(FAppStyle::Get(), "FlatButton.Default").HAlign(HAlign_Center)
						.ToolTipText(LOCTEXT("PasteOffsetTooltip", "Paste the copied offset onto this frame."))
						.IsEnabled_Lambda([this]() { return bHasCopiedOffset; })
						.OnClicked_Lambda([this]() { OnPasteOffset(); return FReply::Handled(); })
						[ SNew(STextBlock).Text(LOCTEXT("Paste", "Paste")) ]
					]
				]
			]
		]

		// === Section: Nudge Controls ===
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 0, 0, 4)
		[
			SNew(STextBlock).Text(LOCTEXT("NudgeControls", "NUDGE"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 0, 0, 4)
		[
			SNew(STextBlock).Text(LOCTEXT("NudgeHint", "WASD or Arrows (Shift = 10px)"))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 0, 0, 12)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[
				SNew(SButton).ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.ToolTipText(LOCTEXT("NudgeUpTooltip", "Nudge Up"))
				.OnClicked_Lambda([this]() { NudgeOffset(0, -1); return FReply::Handled(); })
				[ SNew(SBox).WidthOverride(28).HAlign(HAlign_Center) [ SNew(STextBlock).Text(LOCTEXT("NudgeUp", "W")) ] ]
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 2, 0)
				[
					SNew(SButton).ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
					.OnClicked_Lambda([this]() { NudgeOffset(-1, 0); return FReply::Handled(); })
					[ SNew(SBox).WidthOverride(28).HAlign(HAlign_Center) [ SNew(STextBlock).Text(LOCTEXT("NudgeLeft", "A")) ] ]
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 2, 0)
				[
					SNew(SButton).ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
					.OnClicked_Lambda([this]() { NudgeOffset(0, 1); return FReply::Handled(); })
					[ SNew(SBox).WidthOverride(28).HAlign(HAlign_Center) [ SNew(STextBlock).Text(LOCTEXT("NudgeDown", "S")) ] ]
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton).ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
					.OnClicked_Lambda([this]() { NudgeOffset(1, 0); return FReply::Handled(); })
					[ SNew(SBox).WidthOverride(28).HAlign(HAlign_Center) [ SNew(STextBlock).Text(LOCTEXT("NudgeRight", "D")) ] ]
				]
			]
		];

	return ControlsBox.ToSharedRef();
}

TSharedRef<SWidget> SSpriteEditorPanel::BuildOnionSkinsPanel()
{
	// Same vertical channel stack the Root Motion tool uses, on the shared property grid. The
	// former side-by-side column mixer with vertical sliders read as a different tool entirely.
	FProfileOnionChannelArgs Onion;
	Onion.Label = LOCTEXT("MixerOnion", "Onion");
	Onion.LabelColor = FLinearColor(0.5f, 0.5f, 1.0f);
	Onion.Tooltip = LOCTEXT("MixerOnionEnabledTip", "Show previous frames behind the current frame.");
	Onion.IsChecked = TAttribute<ECheckBoxState>::CreateLambda([this]()
		{ return bShowOnionSkin ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; });
	Onion.OnCheckStateChanged = [this](ECheckBoxState State)
		{ bShowOnionSkin = State == ECheckBoxState::Checked; InvalidateSpriteEditorCanvas(); };
	Onion.Opacity = TAttribute<float>::CreateLambda([this]() { return OnionSkinOpacity; });
	Onion.OnOpacityChanged = [this](float Value) { OnionSkinOpacity = Value; InvalidateSpriteEditorCanvas(); };
	Onion.FrameCount = TAttribute<int32>::CreateLambda([this]() { return OnionSkinFrames; });
	Onion.OnFrameCountChanged = [this](int32 Value) { OnionSkinFrames = Value; InvalidateSpriteEditorCanvas(); };

	FProfileOnionChannelArgs Forward;
	Forward.Label = LOCTEXT("MixerForward", "Forward");
	Forward.LabelColor = FLinearColor(0.5f, 1.0f, 0.5f);
	Forward.Tooltip = LOCTEXT("MixerForwardEnabledTip", "Show following frames ahead of the current frame.");
	Forward.IsChecked = TAttribute<ECheckBoxState>::CreateLambda([this]()
		{ return bShowForwardOnionSkin ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; });
	Forward.OnCheckStateChanged = [this](ECheckBoxState State)
		{ bShowForwardOnionSkin = State == ECheckBoxState::Checked; InvalidateSpriteEditorCanvas(); };
	// Both onion directions share one opacity value, as they did in the previous mixer.
	Forward.Opacity = TAttribute<float>::CreateLambda([this]() { return OnionSkinOpacity; });
	Forward.OnOpacityChanged = [this](float Value) { OnionSkinOpacity = Value; InvalidateSpriteEditorCanvas(); };
	Forward.FrameCount = TAttribute<int32>::CreateLambda([this]() { return ForwardOnionSkinFrames; });
	Forward.OnFrameCountChanged = [this](int32 Value) { ForwardOnionSkinFrames = Value; InvalidateSpriteEditorCanvas(); };

	// The reference channel pins one sprite, so it carries no frame stepper; its Set / Go To /
	// Clear actions follow as their own row.
	FProfileOnionChannelArgs Reference;
	Reference.Label = LOCTEXT("MixerRef", "Reference");
	Reference.LabelColor = FLinearColor(1.0f, 0.8f, 0.4f);
	Reference.Tooltip = LOCTEXT("MixerReferenceEnabledTip", "Show the pinned reference frame.");
	Reference.bShowFrameCount = false;
	Reference.IsChecked = TAttribute<ECheckBoxState>::CreateLambda([this]()
	{
		return bShowReferenceSprite && ReferenceFlipbookIndex != INDEX_NONE
			? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	});
	Reference.OnCheckStateChanged = [this](ECheckBoxState State)
	{
		if (State == ECheckBoxState::Checked)
		{
			if (ReferenceFlipbookIndex == INDEX_NONE && Model.IsValid())
			{
				SetReferenceSprite(Model->GetSelectedFlipbookIndex(), Model->GetSelectedFrameIndex());
			}
			else
			{
				bShowReferenceSprite = ReferenceFlipbookIndex != INDEX_NONE;
			}
		}
		else
		{
			bShowReferenceSprite = false;
		}
		InvalidateSpriteEditorCanvas();
	};
	Reference.Opacity = TAttribute<float>::CreateLambda([this]() { return ReferenceSpriteOpacity; });
	Reference.OnOpacityChanged = [this](float Value) { ReferenceSpriteOpacity = Value; InvalidateSpriteEditorCanvas(); };

	auto MakeReferenceAction = [](
		const FText& Label,
		const FText& Tooltip,
		TFunction<FReply()> OnClicked,
		TAttribute<bool> IsEnabled = TAttribute<bool>()) -> TSharedRef<SWidget>
	{
		TSharedRef<SButton> Button = SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.ContentPadding(FMargin(4.0f, 1.0f))
			.ToolTipText(Tooltip)
			.OnClicked_Lambda([OnClicked]() { return OnClicked ? OnClicked() : FReply::Handled(); })
			[
				SNew(STextBlock)
				.Text(Label)
				.Justification(ETextJustify::Center)
				.Font(FProfilePropertyRowUtils::GetPropertyFont())
			];
		if (IsEnabled.IsSet() || IsEnabled.IsBound())
		{
			Button->SetEnabled(IsEnabled);
		}
		return Button;
	};

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			FProfilePropertyRowUtils::MakeOnionChannelRow(Onion)
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			FProfilePropertyRowUtils::MakeOnionChannelRow(Forward)
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			FProfilePropertyRowUtils::MakeOnionChannelRow(Reference)
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			FProfilePropertyRowUtils::MakeRow(
				LOCTEXT("ReferenceFrameLabel", "Reference Frame"),
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 2, 0)
				[
					MakeReferenceAction(
						LOCTEXT("SetRef", "Set"),
						LOCTEXT("SetRefTip", "Set the current frame as the reference."),
						[this]()
						{
							if (Model.IsValid())
							{
								SetReferenceSprite(Model->GetSelectedFlipbookIndex(), Model->GetSelectedFrameIndex());
							}
							return FReply::Handled();
						})
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2, 0)
				[
					MakeReferenceAction(
						LOCTEXT("GoToRef", "Go To"),
						LOCTEXT("GoToRefTip", "Navigate to the reference flipbook and frame."),
						[this]()
						{
							if (Asset.IsValid() && Asset->Flipbooks.IsValidIndex(ReferenceFlipbookIndex))
							{
								OnFlipbookSelected(ReferenceFlipbookIndex);
								OnFrameSelected(FMath::Clamp(
									ReferenceFrameIndex, 0, FMath::Max(0, GetCurrentFrameCount() - 1)));
							}
							return FReply::Handled();
						},
						TAttribute<bool>::CreateLambda([this]()
						{
							return Asset.IsValid() && Asset->Flipbooks.IsValidIndex(ReferenceFlipbookIndex);
						}))
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2, 0, 0, 0)
				[
					MakeReferenceAction(
						LOCTEXT("ClearReference", "Clear"),
						LOCTEXT("ClearReferenceTip", "Clear the pinned reference frame."),
						[this]()
						{
							ClearReferenceSprite();
							return FReply::Handled();
						},
						TAttribute<bool>::CreateLambda([this]()
						{
							return ReferenceFlipbookIndex != INDEX_NONE;
						}))
				],
				FText::GetEmpty(), 0.0f, 0.0f)
		];
}

TSharedRef<SWidget> SSpriteEditorPanel::BuildBatchPanel()
{

	if (AlignActionOptions.Num() == 0)
	{
		AlignActionOptions.Add(MakeShared<FString>(TEXT("Current Frame's Offset")));
		AlignActionOptions.Add(MakeShared<FString>(TEXT("Reset (0, 0)")));
		AlignActionOptions.Add(MakeShared<FString>(TEXT("Mirror X")));
		AlignActionOptions.Add(MakeShared<FString>(TEXT("Mirror Y")));
		AlignActionOptions.Add(MakeShared<FString>(TEXT("Custom Offset")));
		AlignTargetOptions.Add(MakeShared<FString>(TEXT("All Frames")));
		AlignTargetOptions.Add(MakeShared<FString>(TEXT("Selected Frames")));
		AlignTargetOptions.Add(MakeShared<FString>(TEXT("Remaining Frames")));
		AlignTargetOptions.Add(MakeShared<FString>(TEXT("Custom Range")));
	}

	TSharedPtr<SVerticalBox> ControlsBox;
	SAssignNew(ControlsBox, SVerticalBox);

	ControlsBox->AddSlot().AutoHeight().Padding(0, 0, 0, 4)
	[
		SNew(STextBlock).Text(LOCTEXT("BatchOffsetTools", "Batch Offset Tools"))
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
	];

	// Sentence rows through the shared details-style row helper.
	ControlsBox->AddSlot().AutoHeight()
	[
		FProfilePropertyRowUtils::MakeRow(
			LOCTEXT("AlignApplyLabel", "Apply"),
			FProfilePropertyRowUtils::MakeStringCombo(&AlignActionOptions, &AlignBatchActionIndex))
	];

	ControlsBox->AddSlot().AutoHeight()
	[
		SNew(SBox)
		.Visibility_Lambda([this]() { return AlignBatchActionIndex == 4 ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			FProfilePropertyRowUtils::MakeRow(
				LOCTEXT("CustomOffsetLabel", "Offset"),
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 2, 0)
				[
					SNew(SSpinBox<int32>).MinValue(-999).MaxValue(999)
					.ToolTipText(LOCTEXT("CustomXTip", "Custom offset X"))
					.Value_Lambda([this]() { return AlignBatchCustomValue.X; })
					.OnValueChanged_Lambda([this](int32 V) { AlignBatchCustomValue.X = V; })
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[
					SNew(SSpinBox<int32>).MinValue(-999).MaxValue(999)
					.ToolTipText(LOCTEXT("CustomYTip", "Custom offset Y"))
					.Value_Lambda([this]() { return AlignBatchCustomValue.Y; })
					.OnValueChanged_Lambda([this](int32 V) { AlignBatchCustomValue.Y = V; })
				],
				FText::GetEmpty(), 0.0f, 0.0f)
		]
	];

	ControlsBox->AddSlot().AutoHeight()
	[
		FProfilePropertyRowUtils::MakeRow(
			LOCTEXT("AlignToLabel", "to"),
			FProfilePropertyRowUtils::MakeStringCombo(&AlignTargetOptions, &AlignBatchTargetIndex))
	];

	ControlsBox->AddSlot().AutoHeight()
	[
		SNew(SBox)
		.Visibility_Lambda([this]() { return AlignBatchTargetIndex == 3 ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			FProfilePropertyRowUtils::MakeRow(
				LOCTEXT("AlignRangeLabel", "Range"),
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 4, 0)
				[
					SNew(SSpinBox<int32>).MinValue(0)
					.ToolTipText(LOCTEXT("AlignRangeFromTip", "First frame of the range"))
					.MaxValue_Lambda([this]() { return FMath::Max(0, GetCurrentFrameCount() - 1); })
					.Value_Lambda([this]() { return AlignBatchRangeStart; })
					.OnValueChanged_Lambda([this](int32 V) { AlignBatchRangeStart = V; })
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
				[
					SNew(STextBlock).Text(LOCTEXT("AlignRangeJoin", "to")).Font(FProfilePropertyRowUtils::GetPropertyFont())
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[
					SNew(SSpinBox<int32>).MinValue(0)
					.ToolTipText(LOCTEXT("AlignRangeToTip", "Last frame of the range"))
					.MaxValue_Lambda([this]() { return FMath::Max(0, GetCurrentFrameCount() - 1); })
					.Value_Lambda([this]() { return AlignBatchRangeEnd; })
					.OnValueChanged_Lambda([this](int32 V) { AlignBatchRangeEnd = V; })
				],
				FText::GetEmpty(), 0.0f, 0.0f)
		]
	];

	ControlsBox->AddSlot().AutoHeight().Padding(0, 4, 0, 12)
	[
		SNew(SButton).ButtonStyle(FAppStyle::Get(), "FlatButton.Default").HAlign(HAlign_Center)
		.IsEnabled_Lambda([this]() {
			return AlignBatchTargetIndex != 1 || Model->GetSelectedFrames().Num() > 0;
		})
		.OnClicked_Lambda([this]() { OnApplyAlignmentBatchOperation(); return FReply::Handled(); })
		[ SNew(STextBlock).Text(LOCTEXT("AlignBatchApply", "Apply")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 9)) ]
	];

	ControlsBox->AddSlot().FillHeight(1.0f) [ SNullWidget::NullWidget ];

	return ControlsBox.ToSharedRef();
}

// ==========================================
// Refresh functions
// ==========================================

void SSpriteEditorPanel::RefreshSpriteEditorFrameList()
{
	if (!SpriteEditorFrameListBox.IsValid() || !Asset.IsValid() || !Model.IsValid()) return;

	SpriteEditorFrameListBox->ClearChildren();

	const int32 SelectedFlipbookIndex = Model->GetSelectedFlipbookIndex();
	int32 SelectedFrameIndex = Model->GetSelectedFrameIndex();
	const int32 SelectedExcludedFrameIndex = Model->GetSelectedExcludedFrameIndex();

	const FFlipbookProfileEntry* Anim = GetCurrentFlipbookData();
	if (!Anim) return;

	UPaperFlipbook* Flipbook = GetPreviewFlipbook();

	const int32 ActiveFrameCount = Paper2DPlusEditor::SpriteEditorPanelUtils::PrepareFrameStripFrameCount(Asset.Get(), SelectedFlipbookIndex);
	if (ActiveFrameCount > 0)
	{
		SelectedFrameIndex = FMath::Clamp(SelectedFrameIndex, 0, ActiveFrameCount - 1);
		if (Model->GetSelectedFrameIndex() != SelectedFrameIndex) Model->SetSelectedFrame(SelectedFrameIndex);
	}
	else
	{
		if (Model->GetSelectedFrameIndex() != 0) Model->SetSelectedFrame(0);
	}

	struct FDisplayedEntry
	{
		bool bExcluded = false;
		int32 SourceFrameIndex = INDEX_NONE;
		int32 ActiveFrameIndex = INDEX_NONE;
		int32 ExcludedFrameIndex = INDEX_NONE;
		const FFrameHitboxData* FrameData = nullptr;
		const FSpriteExtractionInfo* ExtractionInfo = nullptr;
		UPaperSprite* Sprite = nullptr;
	};

	TArray<FDisplayedEntry> DisplayEntries;
	DisplayEntries.Reserve(ActiveFrameCount + Anim->CombatData.ExcludedFrames.Num());

	for (int32 ActiveIndex = 0; ActiveIndex < ActiveFrameCount; ++ActiveIndex)
	{
		const FSpriteExtractionInfo* ExtrInfo = Anim->CombatData.FrameExtractionInfo.IsValidIndex(ActiveIndex) ? &Anim->CombatData.FrameExtractionInfo[ActiveIndex] : nullptr;
		const int32 SourceIndex = (ExtrInfo && ExtrInfo->SourceFrameIndex != INDEX_NONE) ? ExtrInfo->SourceFrameIndex : ActiveIndex;
		UPaperSprite* Sprite = (Flipbook && ActiveIndex < Flipbook->GetNumKeyFrames()) ? Flipbook->GetKeyFrameChecked(ActiveIndex).Sprite : nullptr;

		FDisplayedEntry& E = DisplayEntries.AddDefaulted_GetRef();
		E.bExcluded = false;
		E.SourceFrameIndex = SourceIndex;
		E.ActiveFrameIndex = ActiveIndex;
		E.FrameData = Anim->CombatData.Frames.IsValidIndex(ActiveIndex) ? &Anim->CombatData.Frames[ActiveIndex] : nullptr;
		E.ExtractionInfo = ExtrInfo;
		E.Sprite = Sprite;
	}

	for (int32 ExcludedIndex = 0; ExcludedIndex < Anim->CombatData.ExcludedFrames.Num(); ++ExcludedIndex)
	{
		const FExcludedFlipbookFrameData& ExcludedFrame = Anim->CombatData.ExcludedFrames[ExcludedIndex];
		const int32 SourceIndex = ExcludedFrame.ExtractionInfo.SourceFrameIndex != INDEX_NONE
			? ExcludedFrame.ExtractionInfo.SourceFrameIndex : (ActiveFrameCount + ExcludedIndex);

		FDisplayedEntry& E = DisplayEntries.AddDefaulted_GetRef();
		E.bExcluded = true;
		E.SourceFrameIndex = SourceIndex;
		E.ExcludedFrameIndex = ExcludedIndex;
		E.FrameData = &ExcludedFrame.FrameData;
		E.ExtractionInfo = &ExcludedFrame.ExtractionInfo;
		E.Sprite = ExcludedFrame.KeyFrame.Sprite;
	}

	DisplayEntries.Sort([](const FDisplayedEntry& A, const FDisplayedEntry& B)
	{
		if (A.SourceFrameIndex != B.SourceFrameIndex) return A.SourceFrameIndex < B.SourceFrameIndex;
		if (A.bExcluded != B.bExcluded) return !A.bExcluded;
		if (!A.bExcluded && !B.bExcluded) return A.ActiveFrameIndex < B.ActiveFrameIndex;
		return A.ExcludedFrameIndex < B.ExcludedFrameIndex;
	});

	for (const FDisplayedEntry& Entry : DisplayEntries)
	{
		const int32 DisplayNumber = Entry.SourceFrameIndex != INDEX_NONE ? Entry.SourceFrameIndex + 1 : 0;
		const bool bExcluded = Entry.bExcluded;
		const int32 ActiveFrameIndex = Entry.ActiveFrameIndex;
		const int32 ExcludedFrameIndex = Entry.ExcludedFrameIndex;
		UPaperSprite* CapturedSprite = Entry.Sprite;

		TSharedRef<TSharedPtr<SBox>> HoverTargetRef = MakeShared<TSharedPtr<SBox>>();
		TSharedRef<TWeakPtr<SWidget>> CellHoverRef = MakeShared<TWeakPtr<SWidget>>();
		const FText ToggleText = bExcluded ? LOCTEXT("FrameRestoreGlyph", "+") : LOCTEXT("FrameExcludeGlyph", "-");
		const FText ToggleToolTip = bExcluded ? LOCTEXT("RestoreFrameTooltip", "Restore this frame") : LOCTEXT("ExcludeFrameTooltip", "Exclude this frame");

		TSharedRef<SWidget> OverlayContent = SNew(SOverlay)
			+ SOverlay::Slot()
			[
				bExcluded
					? StaticCastSharedRef<SWidget>(SNew(SBorder)
						.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush")).Padding(0)
						.BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.42f))
						.Visibility(EVisibility::HitTestInvisible))
					: StaticCastSharedRef<SWidget>(SNullWidget::NullWidget)
			]
			+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(FMargin(2, 2, 0, 0))
			[
				SAssignNew(*HoverTargetRef, SBox)
				.Visibility_Lambda([this, ActiveFrameIndex, bExcluded, CellHoverRef]() -> EVisibility
				{
					const int32 CurFrame = Model.IsValid() ? Model->GetSelectedFrameIndex() : 0;
					const bool bSel = !bExcluded && (ActiveFrameIndex == CurFrame);
					TSharedPtr<SWidget> PinnedCell = CellHoverRef->Pin();
					const bool bCellHovered = !bExcluded && PinnedCell.IsValid() && PinnedCell->IsHovered();
					return (bExcluded || bSel || bCellHovered) ? EVisibility::Visible : EVisibility::Collapsed;
				})
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "NoBorder")
					.ContentPadding(FMargin(0))
					.ToolTipText(ToggleToolTip)
					.IsEnabled(!bExcluded ? (ActiveFrameCount > 1) : true)
					.OnClicked_Lambda([this, ActiveFrameIndex, ExcludedFrameIndex, bExcluded]() -> FReply
					{
						if (bExcluded) OnRestoreExcludedSpriteEditorFrame(ExcludedFrameIndex);
						else if (ActiveFrameIndex != INDEX_NONE)
						{
							if (Model.IsValid()) Model->SetSelectedFrame(ActiveFrameIndex);
							OnExcludeCurrentSpriteEditorFrame();
						}
						return FReply::Handled();
					})
					[
						SNew(SBorder)
						.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.DarkGroupBorder"))
						.BorderBackgroundColor_Lambda([HoverTargetRef, bExcluded]() -> FSlateColor
						{
							const bool bBtnHovered = HoverTargetRef->IsValid() && (*HoverTargetRef)->IsHovered();
							if (bExcluded) return bBtnHovered ? FLinearColor(0.3f, 0.65f, 0.3f, 1.0f) : FLinearColor(0.2f, 0.45f, 0.2f, 1.0f);
							else return bBtnHovered ? FLinearColor(0.65f, 0.3f, 0.3f, 1.0f) : FLinearColor(0.45f, 0.2f, 0.2f, 1.0f);
						})
						.Padding(FMargin(1))
						[
							SNew(SBox).WidthOverride(12.0f).HeightOverride(12.0f)
							.HAlign(HAlign_Center).VAlign(VAlign_Center)
							[
								SNew(STextBlock).Text(ToggleText)
								.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
								.ColorAndOpacity(FSlateColor(FLinearColor::White))
								.Justification(ETextJustify::Center)
							]
						]
					]
				]
			];

		FFrameStripCellArgs CellArgs;
		CellArgs.Sprite = CapturedSprite;
		CellArgs.FrameIndex = DisplayNumber;
		CellArgs.IsSelected = [this, ActiveFrameIndex, ExcludedFrameIndex, bExcluded]()
		{
			if (!Model.IsValid()) return false;
			if (bExcluded) return ExcludedFrameIndex == Model->GetSelectedExcludedFrameIndex();
			return ActiveFrameIndex == Model->GetSelectedFrameIndex() && Model->GetSelectedExcludedFrameIndex() == INDEX_NONE;
		};
		CellArgs.IsMultiSelected = [this, ActiveFrameIndex, bExcluded]()
		{
			if (!Model.IsValid()) return false;
			return !bExcluded && Model->GetSelectedFrames().Contains(ActiveFrameIndex);
		};
		CellArgs.OnMouseButtonDown = [this, ActiveFrameIndex, ExcludedFrameIndex, bExcluded](const FPointerEvent& MouseEvent) -> FReply
		{
			if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
			{
				if (bExcluded)
				{
					if (Model.IsValid())
					{
						Model->SetSelectedExcludedFrame(ExcludedFrameIndex);
						Model->ClearFrameSelection();
					}
					RefreshSpriteEditorFrameList();
				}
				else
				{
					if (Model.IsValid())
					{
						Model->SetSelectedExcludedFrame(INDEX_NONE);
						Model->HandleFrameClick(ActiveFrameIndex, MouseEvent.IsControlDown(), MouseEvent.IsShiftDown(), GetCurrentFrameCount());
						Model->SetSelectedFrame(ActiveFrameIndex);
					}
					RefreshSpriteEditorFrameList();
				}
				return FReply::Handled();
			}
			return FReply::Unhandled();
		};
		CellArgs.Overlay = OverlayContent;

		TSharedRef<SWidget> CellWidget = FFrameStripCellUtils::Build(CellArgs);
		*CellHoverRef = CellWidget;

		if (!bExcluded)
		{
			TSharedPtr<SFrameDragDropWrapper> FrameWrapper;
			SpriteEditorFrameListBox->AddSlot().AutoWidth().Padding(1, 0)
			[
				SAssignNew(FrameWrapper, SFrameDragDropWrapper)
				[ CellWidget ]
			];
			FrameWrapper->FrameIndex = ActiveFrameIndex;
			// Arm/detect the drag against panel-owned state so the gesture survives the
			// strip rebuild that selecting this frame (on mouse-down) triggers.
			FrameWrapper->OnDragArmFunc = [this](int32 SourceFrameIndex, const FVector2D& ScreenPos)
			{
				bFrameDragArmed = true;
				FrameDragStartScreenPos = ScreenPos;
				FrameDragSourceIndex = SourceFrameIndex;
			};
			FrameWrapper->OnDragDetectFunc = [this](const FVector2D& ScreenPos, bool bLeftButtonDown) -> TSharedPtr<FDragDropOperation>
			{
				if (!bFrameDragArmed) return nullptr;
				if (!bLeftButtonDown) { bFrameDragArmed = false; return nullptr; }
				if (FVector2D::Distance(ScreenPos, FrameDragStartScreenPos) > 5.0f)
				{
					bFrameDragArmed = false;
					return FFrameReorderDragDropOp::New(FrameDragSourceIndex);
				}
				return nullptr;
			};
			FrameWrapper->OnFrameDroppedFunc = [this](int32 From, int32 To)
			{
				OnReorderSpriteEditorFrame(From, To);
			};
		}
		else
		{
			SpriteEditorFrameListBox->AddSlot().AutoWidth().Padding(1, 0) [ CellWidget ];
		}
	}
}

// ==========================================
// Offset operations
// ==========================================

void SSpriteEditorPanel::NudgeOffset(int32 DeltaX, int32 DeltaY)
{
	if (DeltaX == 0 && DeltaY == 0) return;
	if (bIsPlaying) StopPlayback();

	FFlipbookProfileEntry* Anim = GetCurrentFlipbookDataMutable();
	if (!Anim || !Model.IsValid()) return;

	const int32 SelectedFrameIndex = Model->GetSelectedFrameIndex();
	const int32 FrameCount = GetCurrentFrameCount();
	if (SelectedFrameIndex < 0 || SelectedFrameIndex >= FrameCount) return;
	const TSet<int32>& SelectedFrames = Model->GetSelectedFrames();

	bool bInDragGesture = bSpriteEditorDragActive;

	if (bInDragGesture)
	{
		if (!ActiveTransaction.IsValid()) BeginTransaction(LOCTEXT("DragOffset", "Drag Sprite Offset"));
	}
	else
	{
		if (!ActiveTransaction.IsValid()) BeginTransaction(LOCTEXT("NudgeOffset", "Nudge Sprite Offset"));
		if (TSharedPtr<FActiveTimerHandle> OldTimer = NudgeDebounceTimer.Pin())
			UnRegisterActiveTimer(OldTimer.ToSharedRef());
		NudgeDebounceTimer = RegisterActiveTimer(
			0.3f,
			FWidgetActiveTimerDelegate::CreateSP(
				this, &SSpriteEditorPanel::HandleNudgeDebounceTimer));
	}

	auto ApplyDelta = [&](int32 FrameIndex)
	{
		if (FrameIndex < 0 || FrameIndex >= FrameCount) return;
		if (Anim->CombatData.FrameExtractionInfo.Num() <= FrameIndex)
			Anim->CombatData.FrameExtractionInfo.SetNum(FrameIndex + 1);
		Anim->CombatData.FrameExtractionInfo[FrameIndex].SpriteOffset.X += DeltaX;
		Anim->CombatData.FrameExtractionInfo[FrameIndex].SpriteOffset.Y += DeltaY;
	};

	if (SelectedFrames.Num() > 0)
	{
		for (int32 FrameIdx : SelectedFrames) ApplyDelta(FrameIdx);
		if (!SelectedFrames.Contains(SelectedFrameIndex)) ApplyDelta(SelectedFrameIndex);
	}
	else
	{
		ApplyDelta(SelectedFrameIndex);
	}
}

void SSpriteEditorPanel::CommitNudgeTransaction()
{
	NudgeDebounceTimer.Reset();
	if (ActiveTransaction.IsValid()) EndTransaction();
}

EActiveTimerReturnType SSpriteEditorPanel::HandleNudgeDebounceTimer(
	double,
	float)
{
	CommitNudgeTransaction();
	return EActiveTimerReturnType::Stop;
}

void SSpriteEditorPanel::OnOffsetXChanged(int32 NewValue)
{
	if (bIsPlaying) StopPlayback();
	FFlipbookProfileEntry* Anim = GetCurrentFlipbookDataMutable();
	if (!Anim || !Model.IsValid()) return;
	const int32 SelectedFrameIndex = Model->GetSelectedFrameIndex();
	if (SelectedFrameIndex < 0 || SelectedFrameIndex >= GetCurrentFrameCount()) return;
	const int32 CurrentValue = Anim->CombatData.FrameExtractionInfo.IsValidIndex(SelectedFrameIndex)
		? Anim->CombatData.FrameExtractionInfo[SelectedFrameIndex].SpriteOffset.X
		: 0;
	if (CurrentValue == NewValue) return;
	BeginTransaction(LOCTEXT("ChangeOffsetX", "Change Sprite Offset X"));
	if (Anim->CombatData.FrameExtractionInfo.Num() < SelectedFrameIndex + 1) Anim->CombatData.FrameExtractionInfo.SetNum(SelectedFrameIndex + 1);
	Anim->CombatData.FrameExtractionInfo[SelectedFrameIndex].SpriteOffset.X = NewValue;
	EndTransaction();
}

void SSpriteEditorPanel::OnOffsetYChanged(int32 NewValue)
{
	if (bIsPlaying) StopPlayback();
	FFlipbookProfileEntry* Anim = GetCurrentFlipbookDataMutable();
	if (!Anim || !Model.IsValid()) return;
	const int32 SelectedFrameIndex = Model->GetSelectedFrameIndex();
	if (SelectedFrameIndex < 0 || SelectedFrameIndex >= GetCurrentFrameCount()) return;
	const int32 CurrentValue = Anim->CombatData.FrameExtractionInfo.IsValidIndex(SelectedFrameIndex)
		? Anim->CombatData.FrameExtractionInfo[SelectedFrameIndex].SpriteOffset.Y
		: 0;
	if (CurrentValue == NewValue) return;
	BeginTransaction(LOCTEXT("ChangeOffsetY", "Change Sprite Offset Y"));
	if (Anim->CombatData.FrameExtractionInfo.Num() < SelectedFrameIndex + 1) Anim->CombatData.FrameExtractionInfo.SetNum(SelectedFrameIndex + 1);
	Anim->CombatData.FrameExtractionInfo[SelectedFrameIndex].SpriteOffset.Y = NewValue;
	EndTransaction();
}

void SSpriteEditorPanel::OnCopyOffset()
{
	const FFlipbookProfileEntry* Anim = GetCurrentFlipbookData();
	const int32 SelFrame = Model.IsValid() ? Model->GetSelectedFrameIndex() : 0;
	if (!Anim || !Anim->CombatData.FrameExtractionInfo.IsValidIndex(SelFrame)) { CopiedOffset = FIntPoint::ZeroValue; bHasCopiedOffset = true; return; }
	CopiedOffset = Anim->CombatData.FrameExtractionInfo[SelFrame].SpriteOffset;
	bHasCopiedOffset = true;
}

void SSpriteEditorPanel::OnPasteOffset()
{
	if (!bHasCopiedOffset) return;
	FFlipbookProfileEntry* Anim = GetCurrentFlipbookDataMutable();
	if (!Anim || !Model.IsValid()) return;
	const int32 SelFrame = Model->GetSelectedFrameIndex();
	const int32 FrameCount = GetCurrentFrameCount();
	if (SelFrame < 0 || SelFrame >= FrameCount) return;
	const TSet<int32>& SelectedFrames = Model->GetSelectedFrames();
	BeginTransaction(LOCTEXT("PasteOffset", "Paste Sprite Offset"));
	Asset->Modify();
	auto Apply = [&](int32 Idx) {
		if (Idx < 0 || Idx >= FrameCount) return;
		if (Anim->CombatData.FrameExtractionInfo.Num() <= Idx) Anim->CombatData.FrameExtractionInfo.SetNum(Idx + 1);
		Anim->CombatData.FrameExtractionInfo[Idx].SpriteOffset = CopiedOffset;
	};
	if (SelectedFrames.Num() > 0) { for (int32 Idx : SelectedFrames) Apply(Idx); if (!SelectedFrames.Contains(SelFrame)) Apply(SelFrame); }
	else { Apply(SelFrame); }
	EndTransaction();
	RefreshSpriteEditorFrameList();
}

void SSpriteEditorPanel::OnResetOffset()
{
	FFlipbookProfileEntry* Anim = GetCurrentFlipbookDataMutable();
	if (!Anim || !Model.IsValid()) return;
	const int32 SelFrame = Model->GetSelectedFrameIndex();
	if (SelFrame < 0 || SelFrame >= GetCurrentFrameCount()) return;
	const TSet<int32>& SelectedFrames = Model->GetSelectedFrames();
	BeginTransaction(LOCTEXT("ResetOffset", "Reset Sprite Offset"));
	Asset->Modify();
	auto Apply = [&](int32 Idx) {
		if (Anim->CombatData.FrameExtractionInfo.IsValidIndex(Idx))
		{
			Anim->CombatData.FrameExtractionInfo[Idx].SpriteOffset = FIntPoint::ZeroValue;
		}
	};
	if (SelectedFrames.Num() > 0) { for (int32 Idx : SelectedFrames) Apply(Idx); if (!SelectedFrames.Contains(SelFrame)) Apply(SelFrame); }
	else { Apply(SelFrame); }
	EndTransaction();
	RefreshSpriteEditorFrameList();
}

void SSpriteEditorPanel::OnSpriteEditorOffsetChanged(int32 DeltaX, int32 DeltaY) { NudgeOffset(DeltaX, DeltaY); }

void SSpriteEditorPanel::OnApplyAlignmentBatchOperation()
{
	FFlipbookProfileEntry* Anim = GetCurrentFlipbookDataMutable();
	if (!Anim || !Model.IsValid()) return;
	const int32 SelFrame = Model->GetSelectedFrameIndex();
	const TSet<int32>& SelectedFrames = Model->GetSelectedFrames();
	const int32 FrameCount = GetCurrentFrameCount();
	if (FrameCount == 0 || SelFrame < 0 || SelFrame >= FrameCount) return;

	TArray<int32> TargetFrames;
	switch (AlignBatchTargetIndex)
	{
	case 0: for (int32 i = 0; i < FrameCount; i++) TargetFrames.Add(i); break;
	case 1: if (SelectedFrames.Num() > 0) TargetFrames = SelectedFrames.Array(); else TargetFrames.Add(SelFrame); break;
	case 2: for (int32 i = SelFrame; i < FrameCount; i++) TargetFrames.Add(i); break;
	case 3: { int32 First = FMath::Clamp(FMath::Min(AlignBatchRangeStart, AlignBatchRangeEnd), 0, FrameCount - 1); int32 Last = FMath::Clamp(FMath::Max(AlignBatchRangeStart, AlignBatchRangeEnd), 0, FrameCount - 1); for (int32 i = First; i <= Last; i++) TargetFrames.Add(i); } break;
	}
	if (TargetFrames.Num() == 0) return;
	BeginTransaction(LOCTEXT("AlignBatchOp", "Batch Offset Operation"));
	if (Anim->CombatData.FrameExtractionInfo.Num() < FrameCount) Anim->CombatData.FrameExtractionInfo.SetNum(FrameCount);
	switch (AlignBatchActionIndex)
	{
	case 0: { if (!Anim->CombatData.FrameExtractionInfo.IsValidIndex(SelFrame)) break; FIntPoint Src = Anim->CombatData.FrameExtractionInfo[SelFrame].SpriteOffset; for (int32 Idx : TargetFrames) { if (Anim->CombatData.FrameExtractionInfo.IsValidIndex(Idx)) Anim->CombatData.FrameExtractionInfo[Idx].SpriteOffset = Src; } break; }
	case 1: for (int32 Idx : TargetFrames) { if (Anim->CombatData.FrameExtractionInfo.IsValidIndex(Idx)) Anim->CombatData.FrameExtractionInfo[Idx].SpriteOffset = FIntPoint::ZeroValue; } break;
	case 2: for (int32 Idx : TargetFrames) { if (Anim->CombatData.FrameExtractionInfo.IsValidIndex(Idx)) Anim->CombatData.FrameExtractionInfo[Idx].SpriteOffset.X *= -1; } break;
	case 3: for (int32 Idx : TargetFrames) { if (Anim->CombatData.FrameExtractionInfo.IsValidIndex(Idx)) Anim->CombatData.FrameExtractionInfo[Idx].SpriteOffset.Y *= -1; } break;
	case 4: for (int32 Idx : TargetFrames) { if (Anim->CombatData.FrameExtractionInfo.IsValidIndex(Idx)) Anim->CombatData.FrameExtractionInfo[Idx].SpriteOffset = AlignBatchCustomValue; } break;
	}
	EndTransaction();
	RefreshSpriteEditorFrameList();
}

void SSpriteEditorPanel::RefreshCurrentFrameFlipState()
{
	const FFlipbookProfileEntry* Anim = GetCurrentFlipbookData();
	const int32 SelFrame = Model.IsValid() ? Model->GetSelectedFrameIndex() : 0;
	if (!Anim || !Anim->CombatData.FrameExtractionInfo.IsValidIndex(SelFrame)) { bSpriteFlipX = false; bSpriteFlipY = false; return; }
	bSpriteFlipX = Anim->CombatData.FrameExtractionInfo[SelFrame].bFlipX;
	bSpriteFlipY = Anim->CombatData.FrameExtractionInfo[SelFrame].bFlipY;
}

bool SSpriteEditorPanel::CanExcludeCurrentSpriteEditorFrame() const
{
	if (!Asset.IsValid() || !Model.IsValid()) return false;
	const int32 SelFB = Model->GetSelectedFlipbookIndex();
	const int32 SelFrame = Model->GetSelectedFrameIndex();
	if (!Asset->Flipbooks.IsValidIndex(SelFB)) return false;
	UPaperFlipbook* Flipbook = Asset->Flipbooks[SelFB].Identity.Flipbook.LoadSynchronous();
	return Flipbook && Flipbook->GetNumKeyFrames() > 1 && SelFrame >= 0 && SelFrame < Flipbook->GetNumKeyFrames();
}

void SSpriteEditorPanel::OnExcludeCurrentSpriteEditorFrame()
{
	if (!Asset.IsValid() || !Model.IsValid()) return;
	const int32 SelFB = Model->GetSelectedFlipbookIndex();
	const int32 SelFrame = Model->GetSelectedFrameIndex();
	if (!Asset->Flipbooks.IsValidIndex(SelFB)) return;
	UPaperFlipbook* Flipbook = Asset->Flipbooks[SelFB].Identity.Flipbook.LoadSynchronous();
	if (!Flipbook || !CanExcludeCurrentSpriteEditorFrame()) return;
	if (bIsPlaying) StopPlayback();
	BeginTransaction(LOCTEXT("ExcludeSpriteEditorFrameTxn", "Exclude Sprite Frame"));
	Flipbook->SetFlags(RF_Transactional); Flipbook->Modify();
	const bool bExcluded = Asset->ExcludeFlipbookFrame(SelFB, SelFrame);
	EndTransaction();
	if (!bExcluded) return;
	const int32 NewFrameCount = Flipbook->GetNumKeyFrames();
	Model->SetSelectedFrame(NewFrameCount > 0 ? FMath::Clamp(SelFrame, 0, NewFrameCount - 1) : 0);
	if (ReferenceFlipbookIndex == SelFB)
	{
		if (SelFrame < ReferenceFrameIndex) ReferenceFrameIndex--;
		else if (SelFrame == ReferenceFrameIndex) ReferenceFrameIndex = NewFrameCount > 0 ? FMath::Clamp(ReferenceFrameIndex, 0, NewFrameCount - 1) : 0;
	}
	RefreshAfterFrameExclusion();
}

void SSpriteEditorPanel::OnRestoreExcludedSpriteEditorFrame(int32 ExcludedFrameIndex)
{
	if (!Asset.IsValid() || !Model.IsValid()) return;
	const int32 SelFB = Model->GetSelectedFlipbookIndex();
	if (!Asset->Flipbooks.IsValidIndex(SelFB)) return;
	FFlipbookProfileEntry& Anim = Asset->Flipbooks[SelFB];
	if (!Anim.CombatData.ExcludedFrames.IsValidIndex(ExcludedFrameIndex)) return;
	UPaperFlipbook* Flipbook = Anim.Identity.Flipbook.LoadSynchronous();
	if (!Flipbook) return;
	const int32 SourceFrameIndex = Anim.CombatData.ExcludedFrames[ExcludedFrameIndex].ExtractionInfo.SourceFrameIndex;
	BeginTransaction(LOCTEXT("RestoreExcludedSpriteEditorFrameTxn", "Restore Excluded Sprite Frame"));
	Flipbook->SetFlags(RF_Transactional); Flipbook->Modify();
	const bool bRestored = Asset->RestoreExcludedFlipbookFrame(SelFB, ExcludedFrameIndex);
	EndTransaction();
	if (!bRestored || !Asset->Flipbooks.IsValidIndex(SelFB)) return;
	const FFlipbookProfileEntry& UpdatedAnim = Asset->Flipbooks[SelFB];
	const int32 RestoredIndex = FindSpriteEditorFrameIndexBySourceIndex(UpdatedAnim, SourceFrameIndex);
	if (RestoredIndex != INDEX_NONE) Model->SetSelectedFrame(RestoredIndex);
	else Model->SetSelectedFrame(FMath::Clamp(Model->GetSelectedFrameIndex(), 0, FMath::Max(0, Flipbook->GetNumKeyFrames() - 1)));
	RefreshAfterFrameExclusion(true);
}

void SSpriteEditorPanel::OnRestoreAllExcludedSpriteEditorFrames()
{
	if (!Asset.IsValid() || !Model.IsValid()) return;
	const int32 SelFB = Model->GetSelectedFlipbookIndex();
	if (!Asset->Flipbooks.IsValidIndex(SelFB)) return;
	FFlipbookProfileEntry& Anim = Asset->Flipbooks[SelFB];
	if (Anim.CombatData.ExcludedFrames.Num() <= 0) return;
	UPaperFlipbook* Flipbook = Anim.Identity.Flipbook.LoadSynchronous();
	if (!Flipbook) return;
	BeginTransaction(LOCTEXT("RestoreAllExcludedSpriteEditorFramesTxn", "Restore All Excluded Sprite Frames"));
	Flipbook->SetFlags(RF_Transactional); Flipbook->Modify();
	const int32 RestoredCount = Asset->RestoreAllExcludedFlipbookFrames(SelFB);
	EndTransaction();
	if (RestoredCount <= 0) return;
	Model->SetSelectedFrame(FMath::Clamp(Model->GetSelectedFrameIndex(), 0, FMath::Max(0, Flipbook->GetNumKeyFrames() - 1)));
	RefreshAfterFrameExclusion(true);
}

void SSpriteEditorPanel::OnReorderSpriteEditorFrame(int32 FromIndex, int32 ToIndex)
{
	if (!Asset.IsValid() || !Model.IsValid()) return;
	const int32 SelFB = Model->GetSelectedFlipbookIndex();
	if (!Asset->Flipbooks.IsValidIndex(SelFB)) return;
	UPaperFlipbook* Flipbook = Asset->Flipbooks[SelFB].Identity.Flipbook.LoadSynchronous();
	if (!Flipbook) return;
	if (bIsPlaying) StopPlayback();
	BeginTransaction(LOCTEXT("ReorderSpriteEditorFrameTxn", "Reorder Sprite Frame"));
	Flipbook->SetFlags(RF_Transactional); Flipbook->Modify();
	const bool bMoved = Asset->MoveFlipbookFrame(SelFB, FromIndex, ToIndex);
	EndTransaction();
	if (!bMoved) return;
	// MoveFlipbookFrame lands the moved frame at ToIndex; follow it with the selection.
	const int32 NewFrameCount = Flipbook->GetNumKeyFrames();
	Model->SetSelectedFrame(NewFrameCount > 0 ? FMath::Clamp(ToIndex, 0, NewFrameCount - 1) : 0);
	RefreshAfterFrameExclusion();
}

void SSpriteEditorPanel::RefreshAfterFrameExclusion(bool bDismissMenus)
{
	ClearFrameSelection();
	RefreshCurrentFrameFlipState();
	RefreshSpriteEditorFrameList();
	if (bDismissMenus) FSlateApplication::Get().DismissAllMenus();
	if (SpriteEditorCanvas.IsValid()) SpriteEditorCanvas->InvalidateCachedDims();
	// Notify model so other panels refresh too
	if (Model.IsValid()) Model->NotifyAssetDataChanged();
}

TSharedRef<SWidget> SSpriteEditorPanel::BuildSpriteEditorRestoreExcludedMenu()
{
	if (!Asset.IsValid() || !Model.IsValid()) return SNew(STextBlock).Text(LOCTEXT("NoFlipbookSelected", "No flipbook selected"));
	const int32 SelFB = Model->GetSelectedFlipbookIndex();
	if (!Asset->Flipbooks.IsValidIndex(SelFB)) return SNew(STextBlock).Text(LOCTEXT("NoFlipbookSelected2", "No flipbook selected"));
	const FFlipbookProfileEntry& Anim = Asset->Flipbooks[SelFB];
	if (Anim.CombatData.ExcludedFrames.Num() <= 0) return SNew(STextBlock).Text(LOCTEXT("NoExcludedFrames", "No excluded frames"));
	TArray<int32> SortedIndices;
	for (int32 Index = 0; Index < Anim.CombatData.ExcludedFrames.Num(); ++Index) SortedIndices.Add(Index);
	SortedIndices.Sort([&Anim](int32 A, int32 B) { return Anim.CombatData.ExcludedFrames[A].ExtractionInfo.SourceFrameIndex < Anim.CombatData.ExcludedFrames[B].ExtractionInfo.SourceFrameIndex; });
	TSharedRef<SVerticalBox> MenuBox = SNew(SVerticalBox);
	MenuBox->AddSlot().AutoHeight().Padding(2, 0, 2, 4)
	[
		SNew(SButton).ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
		.OnClicked_Lambda([this]() { OnRestoreAllExcludedSpriteEditorFrames(); return FReply::Handled(); })
		[ SNew(STextBlock).Text(LOCTEXT("RestoreAllExcluded", "Restore All")) ]
	];
	for (int32 ExcludedIndex : SortedIndices)
	{
		const FExcludedFlipbookFrameData& EF = Anim.CombatData.ExcludedFrames[ExcludedIndex];
		const int32 SrcNum = FMath::Max(0, EF.ExtractionInfo.SourceFrameIndex) + 1;
		const FText FrameName = EF.FrameData.FrameName.IsEmpty()
			? FText::Format(LOCTEXT("ExcludedFrameDefaultName", "Frame {0}"), FText::AsNumber(SrcNum))
			: FText::FromString(EF.FrameData.FrameName);
		MenuBox->AddSlot().AutoHeight().Padding(2, 0, 2, 2)
		[
			SNew(SButton).ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.OnClicked_Lambda([this, ExcludedIndex]() { OnRestoreExcludedSpriteEditorFrame(ExcludedIndex); return FReply::Handled(); })
			[ SNew(STextBlock).Text(FText::Format(LOCTEXT("RestoreOneFmt", "Restore #{0}: {1}"), FText::AsNumber(SrcNum), FrameName)) ]
		];
	}
	return SNew(SBox).MinDesiredWidth(260.0f).MaxDesiredHeight(280.0f) [ SNew(SScrollBox) + SScrollBox::Slot() [ MenuBox ] ];
}

// ==========================================
// Playback
// ==========================================

void SSpriteEditorPanel::StartPlayback()
{
	if (bIsPlaying) return;
	if (Model.IsValid()
		&& Model->IsDirectionalPreviewEnabled()
		&& !Model->IsDirectionalPreviewRenderable()) return;

	// If queue playback is active on the FlipbookListPanel, don't start local playback
	if (Model.IsValid() && Model->IsQueuePlaying()) return;

	// Playback changes the live frame selection. Finish any canvas/nudge gesture first so its
	// transaction cannot absorb a later edit after playback advances to a different frame.
	FinishActiveEditGesture(/*bReleaseCanvasCapture=*/true);
	ClearFrameSelection();
	bIsPlaying = true;
	bResumeAfterDirectionalPreviewResolves = false;

	const FFlipbookProfileEntry* Anim = GetCurrentFlipbookData();
	UPaperFlipbook* FB = (Anim && !Anim->Identity.Flipbook.IsNull()) ? Anim->Identity.Flipbook.LoadSynchronous() : nullptr;
	if (FB) { FFlipbookTimingData Timing = FFlipbookTimingData::ReadFromFlipbook(FB); PlaybackPosition = Timing.GetFrameStartTime(Model.IsValid() ? Model->GetSelectedFrameIndex() : 0); }
	else { PlaybackPosition = 0.0f; }

	PlaybackTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateSP(this, &SSpriteEditorPanel::OnPlaybackTick), 1.0f / 60.0f);
}

void SSpriteEditorPanel::StopPlayback()
{
	bResumeAfterDirectionalPreviewResolves = false;
	if (!bIsPlaying) return;
	bIsPlaying = false;
	if (PlaybackTickerHandle.IsValid()) { FTSTicker::GetCoreTicker().RemoveTicker(PlaybackTickerHandle); PlaybackTickerHandle.Reset(); }
}

void SSpriteEditorPanel::TogglePlayback()
{
	using namespace Paper2DPlusEditor::DirectionalPreviewPlayback;
	const ECharacterProfileDirectionalPreviewState PreviewState = Model.IsValid()
		? Model->GetDirectionalPreview().State
		: ECharacterProfileDirectionalPreviewState::Base;
	const bool bDirectionalPreviewPaused =
		PreviewState == ECharacterProfileDirectionalPreviewState::Resolving
		|| PreviewState == ECharacterProfileDirectionalPreviewState::Empty;
	const FDecision Decision = Resolve(
		bIsPlaying,
		bResumeAfterDirectionalPreviewResolves,
		bDirectionalPreviewPaused
			? EEvent::UserToggleWhilePaused
			: EEvent::UserToggle);
	if (Decision.bShouldBePlaying)
	{
		StartPlayback();
	}
	else
	{
		StopPlayback();
	}
	bResumeAfterDirectionalPreviewResolves =
		Decision.bResumeAfterDirectionalPreviewResolves;
}

int32 SSpriteEditorPanel::FrameIndexFromPlaybackPosition(const FFlipbookTimingData& Timing, float Position) const
{
	float Acc = 0.0f;
	for (int32 i = 0; i < Timing.FrameDurations.Num(); i++) { float Dur = Timing.GetFrameDurationSeconds(i); if (Position < Acc + Dur) return i; Acc += Dur; }
	return FMath::Max(0, Timing.FrameDurations.Num() - 1);
}

bool SSpriteEditorPanel::OnPlaybackTick(float DeltaTime)
{
	if (!Asset.IsValid() || !Model.IsValid()) return true;

	const FFlipbookProfileEntry* Anim = GetCurrentFlipbookData();
	if (!Anim || Anim->Identity.Flipbook.IsNull()) return true;
	UPaperFlipbook* FB = Anim->Identity.Flipbook.LoadSynchronous();
	if (!FB) return true;
	FFlipbookTimingData Timing = FFlipbookTimingData::ReadFromFlipbook(FB);
	if (Timing.TotalDurationSeconds <= 0.0f) return true;

	if (bPingPongPlayback)
	{
		if (bPlaybackReversed) { PlaybackPosition -= DeltaTime; if (PlaybackPosition < 0.0f) { PlaybackPosition = FMath::Abs(PlaybackPosition); bPlaybackReversed = false; } }
		else { PlaybackPosition += DeltaTime; if (PlaybackPosition >= Timing.TotalDurationSeconds) { PlaybackPosition = Timing.TotalDurationSeconds - (PlaybackPosition - Timing.TotalDurationSeconds); PlaybackPosition = FMath::Max(0.0f, PlaybackPosition); bPlaybackReversed = true; } }
	}
	else if (bPlaybackReversed)
	{
		PlaybackPosition -= DeltaTime;
		if (PlaybackPosition < 0.0f)
		{
			// Modulo wrap so a single large DeltaTime (editor hitch / debugger pause that
			// exceeds the flipbook duration) can't leave the position negative for several
			// ticks — mirrors the forward path's Fmod wrap. TotalDurationSeconds > 0 (guarded above).
			PlaybackPosition = FMath::Fmod(PlaybackPosition, Timing.TotalDurationSeconds);
			if (PlaybackPosition < 0.0f) PlaybackPosition += Timing.TotalDurationSeconds;
		}
	}
	else
	{
		PlaybackPosition += DeltaTime;
		if (PlaybackPosition >= Timing.TotalDurationSeconds) PlaybackPosition = FMath::Fmod(PlaybackPosition, Timing.TotalDurationSeconds);
	}

	int32 NewFrame = FrameIndexFromPlaybackPosition(Timing, PlaybackPosition);
	if (NewFrame != Model->GetSelectedFrameIndex()) { Model->SetSelectedFrame(NewFrame); RefreshSpriteEditorFrameList(); }
	return true;
}

// ==========================================
// Reference sprite
// ==========================================

void SSpriteEditorPanel::InvalidateSpriteEditorCanvas()
{
	if (SpriteEditorCanvas.IsValid())
	{
		SpriteEditorCanvas->Invalidate(EInvalidateWidgetReason::Paint);
	}
}

void SSpriteEditorPanel::SetReferenceSprite(int32 FlipbookIndex, int32 FrameIndex)
{
	ReferenceFlipbookIndex = FlipbookIndex;
	ReferenceFrameIndex = FrameIndex;
	bShowReferenceSprite = true;
	InvalidateSpriteEditorCanvas();
}

void SSpriteEditorPanel::ClearReferenceSprite()
{
	bShowReferenceSprite = false;
	ReferenceFlipbookIndex = INDEX_NONE;
	ReferenceFrameIndex = INDEX_NONE;
	InvalidateSpriteEditorCanvas();
}

#undef LOCTEXT_NAMESPACE
