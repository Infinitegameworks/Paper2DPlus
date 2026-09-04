// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "FrameEventEditor.h"
#include "CharacterProfileEditorModel.h"
#include "SpriteEditorPanel.h"
#include "FlipbookListBuilder.h"
#include "SFrameEventPreviewCanvas.h"
#include "SFrameEventTimelineTrack.h"
#include "SFrameCueTimeline.h"
#include "CurveTrackPanel.h"
#include "FrameCues/Paper2DPlusFrameCue.h"
#include "FrameCues/Paper2DPlusFrameCueDispatch.h"
#include "FrameCues/Paper2DPlusFrameCuePlacementAuthoring.h"
#include "FrameCues/Paper2DPlusSpawnFlipbookCue.h"
#include "FrameCues/Paper2DPlusFrameCueTabPresentation.h"
#include "FrameCues/Paper2DPlusFrameCueTypeAuthoring.h"
#include "FrameCues/Paper2DPlusFrameCueBlueprint.h"
#include "FrameCues/Paper2DPlusFrameCueTypeBirthReady.h"
#include "FrameCues/Paper2DPlusFrameCueTypeEditor.h"
#include "FrameCues/Paper2DPlusFrameCueTypeFactory.h"
#include "FrameCues/SPaper2DPlusFrameCueTypePicker.h"
#include "FrameCues/Paper2DPlusFrameCuePreviewBehavior.h"
#include "FrameCues/Paper2DPlusFrameCuePreviewHost.h"
#include "FrameCues/Paper2DPlusFrameCuePreviewContext.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "EditorCanvasUtils.h"
#include "AnimationTimeline.h"
#include "SlateShortcutUtils.h"
#include "ProfilePropertyRow.h"
#include "PaperFlipbook.h"
#include "PaperSprite.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Images/SImage.h"
#include "ScopedTransaction.h"
#include "PropertyEditorModule.h"
#include "Modules/ModuleManager.h"
#include "Containers/Ticker.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Styling/AppStyle.h"
#include "AssetToolsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "Engine/Engine.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Misc/EngineVersionComparison.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"
#include "Widgets/Notifications/SNotificationList.h"

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

static FText CurveModeToText(const EPaper2DPlusCurveInterp Mode)
{
	switch (Mode)
	{
	case EPaper2DPlusCurveInterp::Constant:
		return LOCTEXT("CurveModeConstant", "Constant");
	case EPaper2DPlusCurveInterp::Cubic:
		return LOCTEXT("CurveModeCubic", "Cubic");
	case EPaper2DPlusCurveInterp::Linear:
	default:
		return LOCTEXT("CurveModeLinear", "Linear");
	}
}

static bool DoesPropertyAffectTimelineLayout(
	const FPropertyChangedEvent& PropertyChangedEvent,
	const UPaper2DPlusCueBase*)
{
	const FName PropertyName = PropertyChangedEvent.GetPropertyName();
	return PropertyName == TEXT("TriggerFrame")
		|| PropertyName == TEXT("StartFrame")
		|| PropertyName == TEXT("FrameCount");
}

static void ShowFailureNotification(const FText& Message)
{
	FNotificationInfo Info(Message);
	Info.bFireAndForget = true;
	Info.bUseLargeFont = false;
	Info.bUseSuccessFailIcons = true;
	Info.ExpireDuration = 6.0f;
	if (TSharedPtr<SNotificationItem> Notification =
		FSlateNotificationManager::Get().AddNotification(Info))
	{
		Notification->SetCompletionState(SNotificationItem::CS_Fail);
	}
}

// Non-stealing confirmation toast: it must never move focus or mutate any editor/model selection. Used
// when an async Cue placement completes but the designer has navigated away from the captured Layer, so
// the reconcile cannot (and must not) yank navigation back to resolve the placed Cue's identity.
static void ShowNonStealingNotification(const FText& Message)
{
	FNotificationInfo Info(Message);
	Info.bFireAndForget = true;
	Info.bUseLargeFont = false;
	Info.bUseSuccessFailIcons = false;
	Info.ExpireDuration = 5.0f;
	if (TSharedPtr<SNotificationItem> Notification =
		FSlateNotificationManager::Get().AddNotification(Info))
	{
		Notification->SetCompletionState(SNotificationItem::CS_Success);
	}
}

}

namespace Paper2DPlusFrameCueEditorAuthoring
{
TArray<FText> GetCreateCueTypeActionLabels()
{
	// The menu consumes this declarative model directly. Keeping one Cue-specific row here prevents
	// the removed Moment/Range generic Blueprint creation actions from drifting back into either host.
	return { LOCTEXT("CreateNewFrameCueType", "New Frame Cue Type...") };
}

EPaper2DPlusFrameCueTypePickerStatus ResolveCueTypePickerStatus(
	const int32 ReadyTypeCount,
	const int32 FilteredTypeCount,
	const int32 RejectedTypeCount,
	const int32 DiscoveryErrorCount,
	const bool bHasSearchText)
{
	if (FilteredTypeCount > 0)
	{
		return DiscoveryErrorCount > 0
			? EPaper2DPlusFrameCueTypePickerStatus::ReadyWithDiscoveryErrors
			: EPaper2DPlusFrameCueTypePickerStatus::Ready;
	}
	if (DiscoveryErrorCount > 0 && ReadyTypeCount <= 0)
	{
		return EPaper2DPlusFrameCueTypePickerStatus::DiscoveryError;
	}
	if (bHasSearchText && ReadyTypeCount > 0)
	{
		return EPaper2DPlusFrameCueTypePickerStatus::NoSearchMatch;
	}
	if (RejectedTypeCount > 0)
	{
		return EPaper2DPlusFrameCueTypePickerStatus::NeedsRepair;
	}
	return EPaper2DPlusFrameCueTypePickerStatus::FirstRun;
}

int32 ResolveCueTypePickerNavigationIndex(
	const int32 CurrentIndex,
	const int32 ItemCount,
	const int32 Direction)
{
	if (ItemCount <= 0)
	{
		return INDEX_NONE;
	}
	const int32 NormalizedDirection = Direction >= 0 ? 1 : -1;
	if (CurrentIndex < 0 || CurrentIndex >= ItemCount)
	{
		return NormalizedDirection > 0 ? 0 : ItemCount - 1;
	}
	return FMath::Clamp(CurrentIndex + NormalizedDirection, 0, ItemCount - 1);
}

EPaper2DPlusFrameCueTypeEditRoute ResolveCueTypeEditRoute(
	const UClass* CueClass,
	UBlueprint*& OutBlueprint)
{
	OutBlueprint = nullptr;
	if (!CueClass)
	{
		return EPaper2DPlusFrameCueTypeEditRoute::None;
	}

	if (CueClass->HasAnyClassFlags(CLASS_Native)
		|| FPaper2DPlusFrameCueTypeAuthoring::ClassifyKind(CueClass)
			== EPaper2DPlusFrameCueTypeKind::Invalid)
	{
		return EPaper2DPlusFrameCueTypeEditRoute::None;
	}

	OutBlueprint = Cast<UBlueprint>(CueClass->ClassGeneratedBy);
	if (!OutBlueprint)
	{
		return EPaper2DPlusFrameCueTypeEditRoute::None;
	}
	if (OutBlueprint->IsA<UPaper2DPlusFrameCueBlueprint>())
	{
		return EPaper2DPlusFrameCueTypeEditRoute::RestrictedSpecialized;
	}
	return EPaper2DPlusFrameCueTypeEditRoute::LegacyRecoveryRequired;
}

void ClampPlacementToAnimation(UPaper2DPlusCueBase* Cue, int32 AnimationKeyFrameCount)
{
	if (!Cue)
	{
		return;
	}

	// A profile entry without a usable flipbook cannot offer a definitive key-frame bound yet. Keep
	// the placement structurally valid at frame zero; validation will continue to report the missing
	// animation relationship.
	const int32 SafeFrameCount = FMath::Max(0, AnimationKeyFrameCount);
	const int32 LastFrame = FMath::Max(0, SafeFrameCount - 1);

	if (UPaper2DPlusCueState* RangeCue = Cast<UPaper2DPlusCueState>(Cue))
	{
		RangeCue->StartFrame = FMath::Clamp(RangeCue->StartFrame, 0, LastFrame);
		const int32 MaxRangeLength = SafeFrameCount > 0
			? FMath::Max(1, SafeFrameCount - RangeCue->StartFrame)
			: 1;
		RangeCue->FrameCount = FMath::Clamp(RangeCue->FrameCount, 1, MaxRangeLength);
	}
	else if (UPaper2DPlusCue* MomentCue = Cast<UPaper2DPlusCue>(Cue))
	{
		MomentCue->TriggerFrame = FMath::Clamp(MomentCue->TriggerFrame, 0, LastFrame);
	}
}

int32 ResolvePlacementIndex(
	const TArray<TObjectPtr<UPaper2DPlusCueBase>>& Cues,
	TWeakObjectPtr<UPaper2DPlusCueBase> CueIdentity)
{
	UPaper2DPlusCueBase* Cue = CueIdentity.Get();
	if (!IsValid(Cue))
	{
		return INDEX_NONE;
	}
	return Cues.IndexOfByPredicate(
		[Cue](const TObjectPtr<UPaper2DPlusCueBase>& Candidate)
		{
			return Candidate.Get() == Cue;
		});
}

UPaper2DPlusCueBase* CreatePlacement(
	UObject* Owner,
	UClass* CueClass,
	int32 AnchorFrame,
	int32 AnimationKeyFrameCount)
{
	if (!Owner
		|| !CueClass
		|| CueClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated)
		|| !CueClass->IsChildOf(UPaper2DPlusCueBase::StaticClass()))
	{
		return nullptr;
	}

	// A Cue Type's generated CDO is the authored default-data authority. Base-typed NewObject
	// initialization can retain generated payload defaults while dropping an inherited native
	// property overridden on that CDO (for example Cue State update emission). Start with a normal
	// transactional instance, then force the engine's reinstancing-safe property copier to apply the
	// complete CDO state without inheriting CDO/archetype object flags.
	UPaper2DPlusCueBase* CueTypeDefaults =
		CastChecked<UPaper2DPlusCueBase>(CueClass->GetDefaultObject());
	UPaper2DPlusCueBase* NewCue = NewObject<UPaper2DPlusCueBase>(
		Owner, CueClass, NAME_None, RF_Transactional);
	UEngine::FCopyPropertiesForUnrelatedObjectsParams CopyParams;
	CopyParams.bDoDelta = false;
	CopyParams.bNotifyObjectReplacement = false;
	UEngine::CopyPropertiesForUnrelatedObjects(CueTypeDefaults, NewCue, CopyParams);
	NewCue->SetPrimaryAnchorFrame(AnchorFrame);
	ClampPlacementToAnimation(NewCue, AnimationKeyFrameCount);
	return NewCue;
}

UPaper2DPlusCueBase* DuplicatePlacement(
	UObject* Owner,
	const UPaper2DPlusCueBase* SourceCue,
	int32 AnimationKeyFrameCount)
{
	if (!Owner || !IsValid(SourceCue))
	{
		return nullptr;
	}

	UPaper2DPlusCueBase* Duplicate = DuplicateObject<UPaper2DPlusCueBase>(SourceCue, Owner);
	if (!Duplicate)
	{
		return nullptr;
	}

	Duplicate->SetFlags(RF_Transactional);
	ClampPlacementToAnimation(Duplicate, AnimationKeyFrameCount);
	return Duplicate;
}

bool EndActiveRangePreview(
	UPaper2DPlusCueBase* Cue,
	EPaper2DPlusFrameCueEndReason EndReason,
	int32 PreviousFrame,
	TSet<TWeakObjectPtr<UPaper2DPlusCueBase>>& ActiveRangeCues,
	FPaper2DPlusFrameCuePreviewHost* PreviewHost,
	FPaper2DPlusFrameCueContext* OutEndContext,
	bool bResetPreviewHost,
	EPaper2DPlusFrameCueEvaluationMode EvaluationMode)
{
	// The ledger is weak, so the lookup below is identity-only: a destroyed placement is simply not
	// found, and only a live Cue can reach the End.
	if (!IsValid(Cue)
		|| !Cue->IsRangeCue()
		|| ActiveRangeCues.Remove(TWeakObjectPtr<UPaper2DPlusCueBase>(Cue)) == 0)
	{
		return false;
	}

	FPaper2DPlusFrameCueContext EndContext =
		FPaper2DPlusFrameCueContext::MakePreview(
			INDEX_NONE,
			PreviousFrame,
			false,
			EvaluationMode);
	if (PreviewHost)
	{
		PreviewHost->PopulateContext(EndContext);
	}
	EndContext.Phase = EPaper2DPlusFrameCuePhase::End;
	EndContext.EndReason = EndReason;
	if (OutEndContext)
	{
		*OutEndContext = EndContext;
	}

	// Remove first, then notify: an adapter callback that re-enters the mutation path cannot emit a
	// second End. Reset after notification so even a faulty adapter that creates work during End cannot
	// leave a preview resource behind. Behavior runs ahead of the adapters at this boundary too, so a
	// placement edited or removed mid-range still receives the End that pairs its Begin.
	if (PreviewHost)
	{
		PreviewHost->BeginDispatchScope();
	}
	ON_SCOPE_EXIT
	{
		if (PreviewHost)
		{
			PreviewHost->EndDispatchScope();
		}
	};
	Paper2DPlusFrameCuePreviewBehavior::NotifyPreview(*Cue, EndContext, PreviewHost);
	if (PreviewHost && bResetPreviewHost)
	{
		PreviewHost->Reset();
	}
	return true;
}
}

// ─── SFrameEventEditor ─────────────────────────────────────────────────────

SFrameEventEditor::SFrameEventEditor() = default;

const FName SFrameEventEditor::CuesPanelId(TEXT("Paper2DPlus.FrameCues.Cues"));
const FName SFrameEventEditor::DetailsPanelId(TEXT("Paper2DPlus.FrameCues.Details"));
const FName SFrameEventEditor::PreviewPanelId(TEXT("Paper2DPlus.FrameCues.Preview"));

void SFrameEventEditor::Construct(const FArguments& InArgs)
{
	Model = InArgs._Model;
	DataProvider = InArgs._DataProvider;
	if (!DataProvider.IsValid())
	{
		DataProvider = MakeShared<FProfileFrameCueDataProvider>(Model);
	}
	HostContract = InArgs._HostContract.IsValid()
		? InArgs._HostContract
		: FProfileToolPanelHostContract::Embedded();
	bHostActive = HostContract.OwnsEmbeddedNavigation();
	PreviewHost = MakeUnique<FPaper2DPlusFrameCuePreviewHost>();
	PreviewHost->SetBeforeHardReset([this]()
	{
		// Blueprint precompile or adapter-registry replacement can invalidate the active implementation.
		// Pair every live Begin while the old class/resources are valid; the host resets afterward.
		ForceEndAllActiveRangedEvents(
			EPaper2DPlusFrameCueEndReason::EditorReset,
			/*bResetPreviewHost=*/false);
		// A running preview reseeds the current frame on its next tick, so a Cue State containing that
		// frame can Begin again against the rebuilt class/adapter set.
		LastPreviewFrame = INDEX_NONE;
	});

	if (Model.IsValid())
	{
		SelectedFlipbookIndex = Model->GetSelectedFlipbookIndex();
		SelectedFrameIndex = Model->GetSelectedFrameIndex();
		PreviewDisplayFrameIndex = SelectedFrameIndex;
	}

	GEditor->RegisterForUndo(this);

	CuePlacementsReplacedHandle = FCoreUObjectDelegates::OnObjectsReplaced.AddSP(
		this, &SFrameEventEditor::HandleCuePlacementsReplaced);

	ModelFlipbookSelectionHandle = Model->OnFlipbookSelectionChanged.AddLambda([this](int32 NewIndex)
	{
		// The model has announced the destination index, but this controller and its curve stack still
		// resolve the source index. Settle focused rename text now so it can never target a same-named
		// curve in the destination animation.
		SettleCurveRenameBeforeScopeChange();
		StopPlaybackForBoundary(EPaper2DPlusFrameCueEndReason::AnimationChanged);
		// StopPlayback ends the previous flipbook's active ranges (mirrors the runtime's
		// HandleFlipbookChanged force-end) and starts the new flipbook fresh.
		LastPreviewFrame = INDEX_NONE;
		SelectedFlipbookIndex = NewIndex;
		SelectedFrameIndex = 0;
		PreviewDisplayFrameIndex = 0;
		SelectedEventIndex = INDEX_NONE;
		SelectedCueIdentity = {};
		TimelineDragCueIdentity = {};
		if (UnifiedTimeline.IsValid()) UnifiedTimeline->SetActiveTrackId(FGuid());
		if (!bHostActive)
		{
			bNeedsRefresh = true;
			return;
		}
		RefreshAll();
	});

	if (DataProvider.IsValid() && DataProvider->IsLayerScoped())
	{
		ModelLayerSelectionHandle = Model->OnLayerSelectionChanged.AddLambda(
			[this](int32)
			{
				SettleCurveRenameBeforeScopeChange();
				// A stable LayerId switch changes the source scope. Finish the old gesture and pair every
				// preview lifecycle before exposing the new source; delayed identities then expire by scope.
				StopPlaybackForBoundary(EPaper2DPlusFrameCueEndReason::SourceRemoved);
				EndTransaction();
				LastPreviewFrame = INDEX_NONE;
				PreviewDisplayFrameIndex = SelectedFrameIndex;
				SelectedEventIndex = INDEX_NONE;
				SelectedCueIdentity = {};
				TimelineDragCueIdentity = {};
				if (UnifiedTimeline.IsValid()) UnifiedTimeline->SetActiveTrackId(FGuid());
				if (!bHostActive)
				{
					bNeedsRefresh = true;
					return;
				}
				RefreshAll();
			});
		ModelLayerVisibilityHandle = Model->OnLayerVisibilityChanged.AddLambda([this]()
		{
			if (!bHostActive) return;
			if (PreviewCanvasWidget.IsValid())
			{
				PreviewCanvasWidget->ResetCachedGeometry();
				PreviewCanvasWidget->Invalidate();
			}
		});
	}

	ModelFrameSelectionHandle = Model->OnFrameSelectionChanged.AddLambda([this]()
	{
		if (Model.IsValid()) SelectedFrameIndex = Model->GetSelectedFrameIndex();
		PreviewDisplayFrameIndex = SelectedFrameIndex;
		if (!bHostActive)
		{
			bNeedsRefresh = true;
			return;
		}
		// A model-driven frame selection is a seek boundary. End the outgoing Range lifecycle and reset
		// every adapter resource before seeding the target frame; playback transitions remain continuous.
		// In-panel frame-strip clicks route through this same model selection, so this is the sole seek
		// dispatch and there is no local echo/double-dispatch.
		if (!bIsPlaying && SelectedFrameIndex != LastPreviewFrame)
		{
			ForceEndAllActiveRangedEvents(EPaper2DPlusFrameCueEndReason::EditorReset);
			LastPreviewFrame = INDEX_NONE;
			DispatchPreviewTransition(SelectedFrameIndex);
		}
		if (FrameStripBox.IsValid()) FrameStripBox->Invalidate(EInvalidateWidgetReason::Paint);
		if (TimelineTrack.IsValid()) TimelineTrack->Invalidate(EInvalidateWidgetReason::Paint);
		if (CurveStack.IsValid()) CurveStack->InvalidateScrub();
		if (PreviewCanvasWidget.IsValid()) PreviewCanvasWidget->Invalidate();
		Invalidate(EInvalidateWidgetReason::Paint);
	});

	ModelGroupCollapseHandle = Model->OnGroupCollapseChanged.AddLambda([this]()
	{
	});

	ModelSearchTextHandle = Model->OnSearchTextChanged.AddLambda([this](const FString&)
	{
	});

	// Retarget on a Base-Profile swap (the Character Layer editor's profile picker calls
	// Model->InitializeFromAsset, which broadcasts OnAssetDataChanged). Mirrors the other model-backed
	// tabs (Hitbox/FrameTiming/RootMotion) so the Frame Events tab refreshes to the new profile instead
	// of showing the old one. Tear down preview state first (the events array changed entirely).
	ModelAssetDataChangedHandle = Model->OnAssetDataChanged.AddLambda([this]()
	{
		if (!bHostActive)
		{
			bNeedsRefresh = true;
			return;
		}
		if (HasActiveTransaction())
		{
			// F7 (ADV-4/CT-R2): pend, never drop — the curve stack's gesture-end poll fires
			// OnGestureSettled, which flushes this.
			bNeedsRefresh = true;
			return;
		}
		if (IsCurveOnlySelfEcho())
		{
			// F8 (ADV-5): a curve write's own deferred broadcast — repaint the rows, skip the full
			// RefreshAll rebuild (the click-flicker source).
			if (CurveStack.IsValid()) CurveStack->RefreshTracks();
			Invalidate(EInvalidateWidgetReason::Paint);
			return;
		}
		StopPlaybackForBoundary(EPaper2DPlusFrameCueEndReason::SourceRemoved);
		LastPreviewFrame = INDEX_NONE;
		RefreshAll();
		// The preview canvas reads the asset via a bindable attribute (Asset_Lambda), so it tracks the
		// new Base Profile automatically — repaint it now so the swap is visible immediately (Codex PR#147).
		if (PreviewCanvasWidget.IsValid()) PreviewCanvasWidget->Invalidate();
	});

	ModelExternalModifiedHandle = Model->OnAssetExternallyModified.AddLambda([this]()
	{
		if (!bHostActive)
		{
			bNeedsRefresh = true;
			return;
		}
		if (HasActiveTransaction())
		{
			// F7: pend, never drop (flushed by OnGestureSettled — see above).
			bNeedsRefresh = true;
			return;
		}
		if (IsCurveOnlySelfEcho())
		{
			// F8: curve-only self-echo — see the OnAssetDataChanged handler.
			if (CurveStack.IsValid()) CurveStack->RefreshTracks();
			Invalidate(EInvalidateWidgetReason::Paint);
			return;
		}
		// Cue identity, timing, class, or payload may have changed outside this panel. Pair any live
		// range and discard adapter resources before rebuilding; then recreate only the still-selected
		// cue's inspection preview. This prevents a removed/changed cue leaving stale output behind.
		StopPlaybackForBoundary(EPaper2DPlusFrameCueEndReason::CueMutation);
		RefreshAll();
		PreviewSelectedCue();
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
			StopPlaybackForBoundary(EPaper2DPlusFrameCueEndReason::AnimationChanged);
			bResumeAfterDirectionalPreviewResolves =
				Decision.bResumeAfterDirectionalPreviewResolves;
		}
		else if (Model->IsDirectionalPreviewRenderable())
		{
			bResumeAfterDirectionalPreviewResolves =
				Decision.bResumeAfterDirectionalPreviewResolves;
			if (Decision.bShouldBePlaying && !bIsPlaying && bHostActive)
			{
				TogglePlayback();
			}
		}
		else
		{
			StopPlaybackForBoundary(EPaper2DPlusFrameCueEndReason::AnimationChanged);
		}
		if (!bHostActive)
		{
			bNeedsRefresh = true;
			return;
		}
		if (PreviewCanvasWidget.IsValid())
		{
			PreviewCanvasWidget->ResetCachedGeometry();
			PreviewCanvasWidget->Invalidate();
		}
		// The strip shows the preview's art, so a bearing change must rebuild it with the canvas.
		RefreshFrameStrip();
		InvalidatePreviewPanel();
		Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
	});

	if (HostContract.OwnsEmbeddedNavigation())
	{
	ChildSlot
	[
		SNew(SSplitter)
		.Orientation(Orient_Horizontal)

			+ SSplitter::Slot()
			.Value(0.65f)
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(4, 2, 4, 0)
				[
					SNew(STextBlock)
					.Text_Lambda([this]() {
						UPaper2DPlusCharacterProfileAsset* AssetPtr = Model.IsValid() ? Model->GetAsset() : nullptr;
						if (!AssetPtr || !AssetPtr->Flipbooks.IsValidIndex(SelectedFlipbookIndex))
							return FText::FromString(TEXT("No Flipbook"));
						const FFlipbookProfileEntry& Entry = AssetPtr->Flipbooks[SelectedFlipbookIndex];
						if (!GetPreviewFlipbook() && Model.IsValid() && Model->IsDirectionalPreviewEnabled())
						{
							return FText::FromString(Model->GetDirectionalPreview().Reason);
						}
						const int32 FrameCount = GetFrameCount();
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
					.Flipbook_Lambda([this]() -> UPaperFlipbook* { return GetPreviewFlipbook(); })
					.FrameIndex_Lambda([this]() { return PreviewDisplayFrameIndex; })
					// Bindable: a Base-Profile swap (Model->InitializeFromAsset) retargets the shared model to a
					// new profile; the canvas re-resolves the live asset each read so it shows the new profile's
					// sprites/offsets/effect previews instead of the construction-time one (Codex PR#147).
					.Asset_Lambda([this]() { return Model.IsValid() ? TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset>(Model->GetAsset()) : nullptr; })
					.LayerAsset(Cast<UPaper2DPlusCharacterLayerAsset>(DataProvider->GetTransactionTarget()))
					.Model(Model)
					.Cues_Lambda([this]() { return GetFrameCues(); })
					.FlipbookIndex_Lambda([this]() { return SelectedFlipbookIndex; })
					.SelectedEventIndex_Lambda([this]() { return SelectedEventIndex; })
					.PreviewHost(PreviewHost.Get())
					.OnCueOffsetDragStarted(this, &SFrameEventEditor::OnPreviewCueOffsetDragStarted)
					.OnCueOffsetChanged(this, &SFrameEventEditor::OnPreviewCueOffsetChanged)
					.OnCueOffsetDragEnded(this, &SFrameEventEditor::OnPreviewCueOffsetDragEnded)
					.AccessibleText_Lambda([this]()
					{
						return PreviewCanvasWidget.IsValid()
							? PreviewCanvasWidget->GetAccessibleSummaryText()
							: LOCTEXT("PreviewCanvasAccessiblePending", "Frame Cue preview canvas");
					})
				]

				// Persistent Frame Cue + Curve timeline. Its toolbar and empty lanes exist before any Cue.
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					BuildUnifiedTimeline()
				]
			]

			+ SSplitter::Slot()
			.Value(0.35f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					BuildCueDetailsPanel()
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					BuildPreviewPanel()
				]
			]
	];
	}
	else
	{
		// The external workspace owns navigation and every contextual dock. This controller remains the
		// sole preview/transaction owner; only the cue timeline and preview canvas stay central.
		ChildSlot
		[
			BuildCentralWorkspace()
		];
	}

	// The preview canvas owns exactly ONE payload gesture: the Spawn Flipbook Cue's offset gizmo,
	// bracketed by the OnCueOffsetDrag* delegates so this controller stays the sole transaction
	// owner. Every other cue payload remains Details-panel-only.

	// Wire timeline track delegates
	if (TimelineTrack.IsValid())
	{
		TimelineTrack->OnFrameClicked.BindLambda([this](int32 FrameIdx)
		{
			OnFrameClicked(FrameIdx);
		});

		TimelineTrack->OnAddCueRequested.BindLambda(
			[this](const int32 FrameIdx, const FGuid TrackId)
			{
				QueueAddEventPicker(FrameIdx, TrackId);
			});

		TimelineTrack->OnEventSelected.BindLambda([this](int32 EventIdx)
		{
			OnEventSelected(EventIdx);
		});

		TimelineTrack->OnEventDragStarted.BindLambda([this](int32 CueIndex)
		{
			if (!CanMutateLiveSelection())
			{
				return;
			}
			const TArray<TObjectPtr<UPaper2DPlusCueBase>>* Cues = GetFrameCues();
			UPaper2DPlusCueBase* Cue =
				Cues && Cues->IsValidIndex(CueIndex) ? (*Cues)[CueIndex].Get() : nullptr;
			if (!DataProvider.IsValid() || !Cue)
			{
				return;
			}
			TimelineDragCueIdentity = DataProvider->GetCueIdentity(SelectedFlipbookIndex, Cue);
			ResetPreviewForCueMutation(Cue, EPaper2DPlusFrameCueEndReason::CueMutation);
			if (UPaper2DPlusCueBase* CurrentCue = ResolveCueIdentity(TimelineDragCueIdentity))
			{
				BeginTransaction(LOCTEXT("MoveFrameCue", "Move Frame Cue"), CurrentCue);
			}
		});

		TimelineTrack->OnEventDragEnded.BindLambda([this]()
		{
			EndTransaction();
			TimelineDragCueIdentity = {};
			ReconcileSelectedCueIdentity();
			if (FrameStripBox.IsValid()) FrameStripBox->Invalidate(EInvalidateWidgetReason::Paint);
			if (TimelineTrack.IsValid()) TimelineTrack->Invalidate(EInvalidateWidgetReason::Paint);
		});

		TimelineTrack->OnEventFrameChanged.BindLambda([this](int32, int32 NewFrame)
		{
			UPaper2DPlusCueBase* Cue = ResolveCueIdentity(TimelineDragCueIdentity);
			if (!ActiveTransaction.IsValid() || !Cue) return;

			// No Asset->Modify() here — BeginTransaction (called from OnEventDragStarted)
			// already marked the asset modified. Bare Modify() during drag fires
			// OnAssetExternallyModified → deferred RefreshAll, causing jarring rebuilds.

			if (UPaper2DPlusCueState* Ranged = Cast<UPaper2DPlusCueState>(Cue))
			{
				Ranged->StartFrame = NewFrame;
			}
			else if (UPaper2DPlusCue* Moment = Cast<UPaper2DPlusCue>(Cue))
			{
				Moment->TriggerFrame = NewFrame;
			}
			Paper2DPlusFrameCueEditorAuthoring::ClampPlacementToAnimation(
				Cue, DataProvider->GetFrameCount(TimelineDragCueIdentity.Scope));
		});

		TimelineTrack->OnEventDurationChanged.BindLambda([this](int32, int32 NewFrameCount)
		{
			UPaper2DPlusCueBase* Cue = ResolveCueIdentity(TimelineDragCueIdentity);
			if (!ActiveTransaction.IsValid() || !Cue) return;

			// No Asset->Modify() here — BeginTransaction (called from OnEventDragStarted)
			// already marked the asset modified. Bare Modify() during drag fires
			// OnAssetExternallyModified → deferred RefreshAll, causing jarring rebuilds.

			if (UPaper2DPlusCueState* Ranged = Cast<UPaper2DPlusCueState>(Cue))
			{
				Ranged->FrameCount = FMath::Max(1, NewFrameCount);
				Paper2DPlusFrameCueEditorAuthoring::ClampPlacementToAnimation(
					Ranged, DataProvider->GetFrameCount(TimelineDragCueIdentity.Scope));
			}
		});

		TimelineTrack->OnEventTriggerEdgeChanged.BindLambda(
			[this](int32, EPaper2DPlusCueTriggerEdge NewEdge)
		{
			UPaper2DPlusCueBase* Cue = ResolveCueIdentity(TimelineDragCueIdentity);
			if (!ActiveTransaction.IsValid() || !Cue) return;

			// Same rule as the frame/duration drags: BeginTransaction already modified the asset.
			if (UPaper2DPlusCue* Moment = Cast<UPaper2DPlusCue>(Cue))
			{
				Moment->TriggerEdge = NewEdge;
			}
		});

		TimelineTrack->OnEventDuplicated.BindLambda([this](int32 EventIdx)
		{
			DuplicateEvent(EventIdx);
		});

		TimelineTrack->OnEventRemoved.BindLambda([this](int32 EventIdx)
		{
			RemoveEvent(EventIdx);
		});

		TimelineTrack->OnEventTrackChanged.BindLambda([this](int32 EventIdx)
		{
			SelectedEventIndex = EventIdx;
			ReconcileSelectedCueIdentity();
			if (UnifiedTimeline.IsValid())
			{
				UnifiedTimeline->HandleCueSelected(SelectedEventIndex);
				UnifiedTimeline->RefreshTrackHeaders();
			}
			RefreshDetailsPanel();
			if (Model.IsValid()) Model->NotifyAssetDataChanged();
		});
	}

	RefreshAll();
}

SFrameEventEditor::~SFrameEventEditor()
{
	HandleHostDeactivated();

	if (Model.IsValid())
	{
		Model->OnFlipbookSelectionChanged.Remove(ModelFlipbookSelectionHandle);
		Model->OnFrameSelectionChanged.Remove(ModelFrameSelectionHandle);
		Model->OnGroupCollapseChanged.Remove(ModelGroupCollapseHandle);
		Model->OnSearchTextChanged.Remove(ModelSearchTextHandle);
		Model->OnAssetExternallyModified.Remove(ModelExternalModifiedHandle);
		Model->OnAssetDataChanged.Remove(ModelAssetDataChangedHandle);
		Model->OnDirectionalPreviewChanged.Remove(ModelDirectionalPreviewHandle);
		Model->OnLayerSelectionChanged.Remove(ModelLayerSelectionHandle);
		Model->OnLayerVisibilityChanged.Remove(ModelLayerVisibilityHandle);
	}

	if (CuePlacementsReplacedHandle.IsValid())
	{
		FCoreUObjectDelegates::OnObjectsReplaced.Remove(CuePlacementsReplacedHandle);
		CuePlacementsReplacedHandle.Reset();
	}

	if (GEditor)
	{
		GEditor->UnregisterForUndo(this);
	}

	if (PreviewHost)
	{
		PreviewHost->SetBeforeHardReset(TFunction<void()>());
	}
	// SCompoundWidget's base ChildSlot outlives derived members. Detach the complete widget tree while
	// PreviewHost/FPreviewScene is still alive so the SEditorViewport client cannot retain a dangling
	// FPreviewScene pointer during base-class destruction or the preview scene's forced cleanup.
	PreviewCanvasWidget.Reset();
	ChildSlot.DetachWidget();
}

void SFrameEventEditor::HandleCuePlacementsReplaced(
	const TMap<UObject*, UObject*>& Replacements)
{
	if (EditorActiveRangeCues.Num() == 0 || Replacements.Num() == 0)
	{
		return;
	}

	// Deleting or recompiling a Cue Type reinstances its placements: the profile's array is patched to
	// the replacement objects, but this active-range set is plain editor state that nothing patches for
	// it. Follow the replacement here so the next teardown ends the surviving placement instead of
	// reaching for an object that is on its way out. Reinstancing is exactly the case a weak ledger
	// cannot cover on its own — the old object is still alive, so it still resolves — which is why the
	// remap stays.
	TSet<TWeakObjectPtr<UPaper2DPlusCueBase>> Remapped;
	Remapped.Reserve(EditorActiveRangeCues.Num());
	bool bRemappedAny = false;
	bool bDroppedStale = false;
	for (const TWeakObjectPtr<UPaper2DPlusCueBase>& ActiveCue : EditorActiveRangeCues)
	{
		UPaper2DPlusCueBase* LiveCue = ActiveCue.Get();
		if (!LiveCue)
		{
			// Observed, never owned: an entry whose placement is already gone is dropped silently. A
			// freed Cue cannot receive an End, so there is nothing to pair.
			bDroppedStale = true;
			continue;
		}
		UObject* const* Replacement = Replacements.Find(LiveCue);
		if (!Replacement)
		{
			Remapped.Add(LiveCue);
			continue;
		}
		bRemappedAny = true;
		if (UPaper2DPlusCueBase* ReplacementCue = Cast<UPaper2DPlusCueBase>(*Replacement))
		{
			Remapped.Add(ReplacementCue);
		}
	}
	if (bRemappedAny || bDroppedStale)
	{
		EditorActiveRangeCues = MoveTemp(Remapped);
	}
	if (bRemappedAny)
	{
		LastPreviewFrame = INDEX_NONE;
	}
}

TSet<TObjectPtr<UPaper2DPlusCueBase>> SFrameEventEditor::ResolveActiveRangeCues() const
{
	// The shared dispatch helpers are handed live placements only. This widget cannot keep a placement
	// alive (no GC visibility) and cannot be patched by ObjectTools::ForceDeleteObjects (no reference
	// the replacement pass can see), so an entry whose placement was destroyed under the open tab is
	// dropped here rather than dereferenced. This matches the pre-behavior baseline, where
	// ForceEndActiveRanges skipped every !IsValid entry for the same reason.
	TSet<TObjectPtr<UPaper2DPlusCueBase>> LiveActiveRanges;
	LiveActiveRanges.Reserve(EditorActiveRangeCues.Num());
	for (const TWeakObjectPtr<UPaper2DPlusCueBase>& ActiveCue : EditorActiveRangeCues)
	{
		if (UPaper2DPlusCueBase* LiveCue = ActiveCue.Get())
		{
			LiveActiveRanges.Add(LiveCue);
		}
	}
	return LiveActiveRanges;
}

void SFrameEventEditor::StoreActiveRangeCues(
	const TSet<TObjectPtr<UPaper2DPlusCueBase>>& LiveActiveRanges)
{
	// Rebuilt, not merged: whatever the helper did not keep active is no longer active, and any stale
	// entry the resolve step dropped stays dropped.
	EditorActiveRangeCues.Reset();
	EditorActiveRangeCues.Reserve(LiveActiveRanges.Num());
	for (const TObjectPtr<UPaper2DPlusCueBase>& LiveCue : LiveActiveRanges)
	{
		if (UPaper2DPlusCueBase* Cue = LiveCue.Get())
		{
			EditorActiveRangeCues.Add(Cue);
		}
	}
}

int32 SFrameEventEditor::CountLiveActiveRangeCues() const
{
	int32 LiveCount = 0;
	for (const TWeakObjectPtr<UPaper2DPlusCueBase>& ActiveCue : EditorActiveRangeCues)
	{
		if (ActiveCue.IsValid())
		{
			++LiveCount;
		}
	}
	return LiveCount;
}

void SFrameEventEditor::GetContextualPanels(
	TArray<FProfileToolPanelDescriptor>& OutPanels) const
{
	if (!HostContract.UsesExternalNavigation())
	{
		return;
	}

	const TWeakPtr<SFrameEventEditor> WeakController =
		ConstCastSharedRef<SFrameEventEditor>(SharedThis(this));
	auto AddPanel = [&OutPanels, WeakController](
		FName PanelId,
		const FText& Label,
		FName IconName,
		const FText& ToolTip,
		TFunction<TSharedRef<SWidget>(SFrameEventEditor&)> Builder)
	{
		FProfileToolPanelDescriptor Descriptor;
		Descriptor.PanelId = PanelId;
		Descriptor.Label = Label;
		Descriptor.IconName = IconName;
		Descriptor.ToolTip = ToolTip;
		Descriptor.CapabilityId = PanelId;
		Descriptor.IsAvailable = [WeakController]() { return WeakController.IsValid(); };
		Descriptor.WidgetFactory =
			[WeakController, PanelId, Builder = MoveTemp(Builder)]() -> TSharedRef<SWidget>
		{
			const TSharedPtr<SFrameEventEditor> Controller = WeakController.Pin();
			if (!Controller.IsValid())
			{
				return SNullWidget::NullWidget;
			}
			++Controller->ContextPanelBuildCounts.FindOrAdd(PanelId);
			Controller->ContextPanelResolvedFrames.FindOrAdd(PanelId) =
				Controller->Model.IsValid()
					? Controller->Model->GetSelectedFrameIndex()
					: Controller->SelectedFrameIndex;
			Controller->ContextPanelResolvedCues.FindOrAdd(PanelId) =
				Controller->ResolveCueIdentity(Controller->SelectedCueIdentity);
			return Builder(*Controller);
		};
		OutPanels.Add(MoveTemp(Descriptor));
	};

	AddPanel(
		DetailsPanelId,
		LOCTEXT("FrameCueDetailsContext", "Details"),
		TEXT("Icons.Details"),
		LOCTEXT("FrameCueDetailsContextTip", "Inspect the timeline's selected Cue, track, or curve."),
		[](SFrameEventEditor& Controller) { return Controller.BuildCueDetailsPanel(); });
	AddPanel(
		PreviewPanelId,
		LOCTEXT("FrameCuePreviewContext", "Preview"),
		TEXT("Icons.Visible"),
		LOCTEXT(
			"FrameCuePreviewContextTip",
			"Inspect automatic isolated-world behavior preview and clear its owned resources."),
		[](SFrameEventEditor& Controller) { return Controller.BuildPreviewPanel(); });
}

void SFrameEventEditor::HandleHostActivated()
{
	if (HostFocusSeat.IsApplying())
	{
		return;
	}

	bHostActive = true;
	if (Model.IsValid())
	{
		SelectedFlipbookIndex = Model->GetSelectedFlipbookIndex();
		SelectedFrameIndex = Model->GetSelectedFrameIndex();
		PreviewDisplayFrameIndex = SelectedFrameIndex;
	}
	if (CurveStack.IsValid())
	{
		CurveStack->RefreshTracks(/*bForceRebuild=*/ true);
	}
	RefreshAll();
	if (HostFocusSeat.ShouldRequestSeat(*this))
	{
		// A synchronous focus move re-enters SDockTab activation before Slate commits its focus path.
		// Defer one paint so the original activation returns before shortcuts claim keyboard focus.
		HostFocusSeat.TrackTimer(RegisterActiveTimer(
			0.0f,
			FWidgetActiveTimerDelegate::CreateSP(
				this,
				&SFrameEventEditor::ApplyDeferredHostFocus)));
	}
}

EActiveTimerReturnType SFrameEventEditor::ApplyDeferredHostFocus(
	double /*CurrentTime*/,
	float /*DeltaTime*/)
{
	return HostFocusSeat.ApplySeat(SharedThis(this), bHostActive);
}

void SFrameEventEditor::ApplyDeferredHostFocusForTests()
{
	if (const TSharedPtr<FActiveTimerHandle> Timer = HostFocusSeat.GetPendingTimer())
	{
		UnRegisterActiveTimer(Timer.ToSharedRef());
	}
	ApplyDeferredHostFocus(0.0, 0.0f);
}

void SFrameEventEditor::HandleHostDeactivated()
{
	SettleCurveRenameBeforeScopeChange();
	// Lower the write gate first. Capture-loss delegates can synchronously re-enter panel callbacks;
	// they may settle the gesture but cannot start a new mutation against a newly selected tool.
	bHostActive = false;
	// The preview viewport owns one gesture — the Spawn Flipbook offset gizmo. Settle it with the
	// same exactly-once discipline as the timeline tracks; its end delegate skips refresh/preview
	// work because the write gate is already down.
	if (PreviewCanvasWidget.IsValid())
	{
		PreviewCanvasWidget->CancelActiveInteraction();
	}
	if (UnifiedTimeline.IsValid())
	{
		UnifiedTimeline->HandleHostDeactivated();
	}
	else if (TimelineTrack.IsValid())
	{
		TimelineTrack->CancelActiveInteraction();
	}
	if (CurveStack.IsValid())
	{
		CurveStack->HandleHostDeactivated();
	}
	FinishEventDetailsPropertyChange();
	EndTransaction();
	StopPlaybackForBoundary(EPaper2DPlusFrameCueEndReason::EditorReset);
	TimelineDragCueIdentity = {};
	OffsetDragCueIdentity = {};
	WarmedAnimationCueEffectFlipbooks.Reset();
}

int32 SFrameEventEditor::GetPreviewResourceCountForTests() const
{
	return PreviewHost ? PreviewHost->GetOwnedResourceCount() : 0;
}

UPaper2DPlusFrameCuePreviewContext* SFrameEventEditor::GetPreviewContextForTests() const
{
	return PreviewHost ? PreviewHost->GetContext() : nullptr;
}

void SFrameEventEditor::BeginSelectedCueTransactionForTests()
{
	BeginTransaction(
		LOCTEXT("FrameCueTestGestureTransaction", "Edit Frame Cue"),
		ResolveCueIdentity(SelectedCueIdentity));
}

void SFrameEventEditor::BeginTimelineInteractionForTests(bool bResize)
{
	if (TimelineTrack.IsValid())
	{
		TimelineTrack->BeginActiveInteractionForTests(
			ResolveCueIdentity(SelectedCueIdentity), bResize);
	}
}

bool SFrameEventEditor::HasTimelineInteractionForTests() const
{
	return TimelineTrack.IsValid() && TimelineTrack->HasActiveInteractionForTests();
}

bool SFrameEventEditor::CanMutateLiveSelection() const
{
	return bHostActive
		&& DataProvider.IsValid()
		&& DataProvider->HasResolvedScope()
		&& SelectedFlipbookIndex != INDEX_NONE
		&& GetFrameCount() > 0;
}

// ─── SPAWN FLIPBOOK OFFSET GIZMO ────────────────────────────────────────────

void SFrameEventEditor::OnPreviewCueOffsetDragStarted(UPaper2DPlusCueBase* Cue)
{
	if (!CanMutateLiveSelection() || !DataProvider.IsValid() || !Cue)
	{
		return;
	}
	OffsetDragCueIdentity = DataProvider->GetCueIdentity(SelectedFlipbookIndex, Cue);
	ResetPreviewForCueMutation(Cue, EPaper2DPlusFrameCueEndReason::CueMutation);
	if (UPaper2DPlusCueBase* CurrentCue = ResolveCueIdentity(OffsetDragCueIdentity))
	{
		BeginTransaction(
			LOCTEXT("DragSpawnFlipbookOffset", "Drag Spawn Flipbook Offset"),
			CurrentCue);
	}
}

void SFrameEventEditor::OnPreviewCueOffsetChanged(FVector2D NewOffset)
{
	UPaper2DPlusSpawnFlipbookCue* Cue = Cast<UPaper2DPlusSpawnFlipbookCue>(
		ResolveCueIdentity(OffsetDragCueIdentity));
	if (!ActiveTransaction.IsValid() || !Cue)
	{
		return;
	}
	// No Asset->Modify() here — BeginTransaction (called from OnPreviewCueOffsetDragStarted)
	// already marked the asset modified; a bare Modify() during drag fires the deferred
	// external-modified rebuild under the live gesture.
	Cue->Offset = NewOffset;
	// Deliberately no per-move PreviewSelectedCue(): re-running real behavior on every pointer
	// move would spawn a fresh one-shot component per pixel. The viewport ghost follows the
	// authored value live; the real effect re-previews once at drag end.
}

void SFrameEventEditor::OnPreviewCueOffsetDragEnded()
{
	EndTransaction();
	OffsetDragCueIdentity = {};
	ReconcileSelectedCueIdentity();
	if (!bHostActive)
	{
		// Settled by host deactivation: no refresh or re-preview against a tool that is leaving.
		return;
	}
	if (EventDetailsView.IsValid())
	{
		EventDetailsView->ForceRefresh();
	}
	// One real behavior pass at the final offset so the designer sees the actual effect land
	// where the ghost was dropped.
	PreviewSelectedCue();
}

void SFrameEventEditor::BeginOffsetGizmoInteractionForTests()
{
	if (PreviewCanvasWidget.IsValid())
	{
		PreviewCanvasWidget->BeginOffsetInteractionForTests();
	}
}

bool SFrameEventEditor::HasOffsetGizmoInteractionForTests() const
{
	return PreviewCanvasWidget.IsValid()
		&& PreviewCanvasWidget->HasActiveOffsetInteraction();
}

void SFrameEventEditor::ApplyOffsetGizmoDeltaForTests(const FVector& WorldDelta)
{
	if (PreviewCanvasWidget.IsValid())
	{
		PreviewCanvasWidget->ApplyOffsetDragDeltaForTests(WorldDelta);
	}
}

void SFrameEventEditor::EndOffsetGizmoInteractionForTests()
{
	if (PreviewCanvasWidget.IsValid())
	{
		PreviewCanvasWidget->CancelActiveInteraction();
	}
}

// ─── UNDO / REDO ────────────────────────────────────────────────────────────

void SFrameEventEditor::PostUndo(bool bSuccess)
{
	StopPlaybackForBoundary(EPaper2DPlusFrameCueEndReason::CueMutation);
	bNeedsRefresh = true;
	if (bSuccess && bHostActive)
	{
		// Rebuild the event list, frame strip, and timeline so undo/redo isn't a visual no-op.
		// RefreshAll() clears bNeedsRefresh.
		RefreshAll();
	}
}

void SFrameEventEditor::PostRedo(bool bSuccess) { PostUndo(bSuccess); }

void SFrameEventEditor::NotifyPreChange(FProperty*)
{
	if (!bHostActive)
	{
		return;
	}
	// PropertyEditor owns the UObject transaction. This hook only keeps deferred model callbacks from
	// rebuilding the details widget while its spinbox/slider still owns the physical gesture.
	bEventDetailsPropertyChangeActive = true;
}

void SFrameEventEditor::NotifyPostChange(
	const FPropertyChangedEvent&,
	FProperty*)
{
	if (!bHostActive)
	{
		return;
	}
	// Keep the gate raised through Interactive post notifications. The details view's single
	// OnFinishedChangingProperties callback is the gesture boundary that lowers and flushes it.
	bEventDetailsPropertyChangeActive = true;

	// PropertyEditor calls the notify hook before it ends its transaction. Clamp every dependent
	// timing field here so the authored edit and its bound correction remain one undo step. Resolve by
	// placement identity because a PostEdit callback is allowed to reorder the owning array.
	if (UPaper2DPlusCueBase* Cue = ResolveCueIdentity(SelectedCueIdentity))
	{
		Paper2DPlusFrameCueEditorAuthoring::ClampPlacementToAnimation(Cue, GetFrameCount());
	}
}

void SFrameEventEditor::HandleEventDetailsFinishedChangingProperties(
	const FPropertyChangedEvent& PropertyChangedEvent)
{
	if (!bHostActive)
	{
		FinishEventDetailsPropertyChange();
		return;
	}
	// OnFinishedChangingProperties runs after PropertyEditor has closed its transaction. Only refresh
	// transient preview/UI state here; serialized timing was clamped in NotifyPostChange above.
	if (UPaper2DPlusCueBase* Cue = ResolveCueIdentity(SelectedCueIdentity))
	{
		ResetPreviewForCueMutation(Cue, EPaper2DPlusFrameCueEndReason::CueMutation);
	}
	else if (PreviewHost)
	{
		PreviewHost->Reset();
	}
	PreviewSelectedCue();
	// Only timing fields can change the timeline's desired width. Every other details edit is a
	// visual-only repaint.
	if (TimelineTrack.IsValid())
	{
		TimelineTrack->Invalidate(FrameEventEditorInternal::DoesPropertyAffectTimelineLayout(
			PropertyChangedEvent,
			ResolveCueIdentity(SelectedCueIdentity))
			? EInvalidateWidgetReason::Layout
			: EInvalidateWidgetReason::Paint);
	}
	if (FrameStripBox.IsValid()) FrameStripBox->Invalidate(EInvalidateWidgetReason::Paint);
	FinishEventDetailsPropertyChange();
}

void SFrameEventEditor::FinishEventDetailsPropertyChange()
{
	bEventDetailsPropertyChangeActive = false;
	if (bHostActive && bNeedsRefresh && !HasActiveTransaction())
	{
		RefreshAll(); // consumes the pend only after PropertyEditor ended the physical gesture
	}
}

void SFrameEventEditor::FinishEventDetailsPropertyChangeForTests()
{
	FinishEventDetailsPropertyChange();
}

// ─── Transaction helpers ─────────────────────────────────────────────────────

void SFrameEventEditor::BeginTransaction(const FText& Description, UPaper2DPlusCueBase* CueToModify)
{
	if (!bHostActive)
	{
		return;
	}
	// One physical gesture owns one transaction. Ignore duplicate start notifications instead of
	// replacing (and therefore prematurely closing) the live transaction.
	if (ActiveTransaction.IsValid())
	{
		return;
	}

	UObject* TransactionTarget = DataProvider.IsValid()
		? DataProvider->GetTransactionTarget()
		: nullptr;
	if (!IsValid(TransactionTarget))
	{
		return;
	}

	ActiveTransaction = MakeUnique<FScopedTransaction>(Description);
	TransactionTarget->SetFlags(RF_Transactional);
	TransactionTarget->Modify();
	if (IsValid(CueToModify))
	{
		CueToModify->SetFlags(RF_Transactional);
		CueToModify->Modify();
	}
}

void SFrameEventEditor::EndTransaction()
{
	ActiveTransaction.Reset();
	if (bHostActive && bNeedsRefresh && !HasActiveTransaction())
	{
		RefreshAll();
	}
}

bool SFrameEventEditor::HasActiveTransaction() const
{
	// The gesture-aware write gate. The curve-track rows' SCurveEditors open their OWN engine
	// transactions ("Mouse Drag" / "Add Key(s)" / "Delete Key(s)") and call Asset->Modify() directly —
	// THE one sanctioned exception to the "panel-owned BeginTransaction/EndTransaction" template.
	// Every such Modify re-schedules the model's deferred external-modify broadcast, which lands the
	// NEXT tick — mid-drag — so transaction sniffing alone can't suppress it: this gate must also
	// report true while a curve gesture is live (engine transaction active + a row holds mouse
	// capture, or the stack's write-in-progress self-echo flag is up — Rule A,
	// docs/solutions/ue-graph-editor-over-flat-data-patterns.md). Without it, the broadcast handlers
	// below would RefreshAll-rebuild the tab under the live drag.
	if (ActiveTransaction.IsValid() || bEventDetailsPropertyChangeActive)
	{
		return true;
	}
	// The preview viewport's offset-gizmo drag is a live gesture even between transaction
	// heartbeats; without this the deferred external-modified broadcast rebuilds the tab under it.
	if (PreviewCanvasWidget.IsValid() && PreviewCanvasWidget->HasActiveOffsetInteraction())
	{
		return true;
	}
	return CurveStack.IsValid() && (CurveStack->HasActiveTransaction() || CurveStack->IsCurveGestureLive());
}

bool SFrameEventEditor::IsCurveOnlySelfEcho() const
{
	// F8 (ADV-5): the deferred-tick analogue of the Rule A self-echo flag. A curve write's
	// Asset->Modify() fires the model's OnObjectModified, whose broadcast lands the NEXT tick — after
	// the synchronous self-echo flag has already dropped. The stack stamps GFrameCounter at every
	// self-originated curve write; a broadcast landing within 1-2 frames of that stamp is that write's
	// own echo, so a full RefreshAll (event-list/details ClearChildren rebuilds) would only flicker.
	// Known trade-off: a GENUINE external write landing inside the same 2-frame window is
	// misclassified curve-only and gets RefreshTracks instead of RefreshAll — the next broadcast
	// outside the window converges it.
	if (!CurveStack.IsValid())
	{
		return false;
	}
	const uint64 Stamp = CurveStack->GetLastSelfCurveWriteFrame();
	return Stamp != 0 && GFrameCounter >= Stamp && (GFrameCounter - Stamp) <= 2;
}

// ─── TOOLBAR ─────────────────────────────────────────────────────────────────

TSharedRef<SWidget> SFrameEventEditor::BuildUnifiedTimeline()
{
	TSharedRef<SWidget> Ruler = SAssignNew(FrameStripBox, SHorizontalBox);
	TSharedRef<SWidget> Curves = BuildCurveTrackContent();
	TSharedRef<SWidget> CurveLegend = CurveStack.IsValid()
		? CurveStack->DetachLegendContentForExternalLayout()
		: SNullWidget::NullWidget;
	TSharedRef<SFrameCueTimeline> Timeline = SAssignNew(UnifiedTimeline, SFrameCueTimeline)
		.DataProvider(DataProvider)
		.Asset_Lambda([this]()
		{
			return Model.IsValid()
				? TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset>(Model->GetAsset())
				: TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset>();
		})
		.Cues_Lambda([this]() { return GetFrameCues(); })
		.SelectedFlipbookIndex_Lambda([this]() { return SelectedFlipbookIndex; })
		.SelectedCueIndex_Lambda([this]() { return SelectedEventIndex; })
		.SelectedFrameIndex_Lambda([this]() { return SelectedFrameIndex; })
		.RulerContent(Ruler)
		.CurveContent(Curves)
		.CurveLegendContent(CurveLegend)
		.OnAddCue_Lambda([this]() { ShowAddEventPicker(); })
		.GetAddCurveMenuContent([this]() -> TSharedRef<SWidget>
		{
			return CurveStack.IsValid()
				? CurveStack->BuildAddCurveMenu()
				: SNullWidget::NullWidget;
		})
		.OnStructureChanged_Lambda([this]()
		{
			if (Model.IsValid())
			{
				Model->NotifyAssetDataChanged();
			}
		})
		.OnPrimarySelectionChanged_Lambda([this](
			const Paper2DPlusFrameCueTimeline::FPrimarySelection& Selection)
		{
			HandleTimelinePrimarySelectionChanged(Selection);
		});
	TimelineTrack = UnifiedTimeline->GetCueLane();
	if (SelectedEventIndex != INDEX_NONE)
	{
		UnifiedTimeline->HandleCueSelected(SelectedEventIndex);
	}
	return Timeline;
}

TSharedRef<SWidget> SFrameEventEditor::BuildCentralWorkspace()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4, 2, 4, 0)
		[
			SNew(STextBlock)
			.Text_Lambda([this]()
			{
				UPaper2DPlusCharacterProfileAsset* AssetPtr = Model.IsValid() ? Model->GetAsset() : nullptr;
				if (!AssetPtr || !AssetPtr->Flipbooks.IsValidIndex(SelectedFlipbookIndex))
				{
					return LOCTEXT("NoFrameCueFlipbook", "No animation selected");
				}
				const FFlipbookProfileEntry& Entry = AssetPtr->Flipbooks[SelectedFlipbookIndex];
				if (!GetPreviewFlipbook() && Model.IsValid() && Model->IsDirectionalPreviewEnabled())
				{
					return FText::FromString(Model->GetDirectionalPreview().Reason);
				}
				return FText::Format(
					LOCTEXT("FrameCueTitleFmt", "{0}  Frame {1}/{2}"),
					FText::FromString(Entry.Identity.FlipbookName),
					FText::AsNumber(SelectedFrameIndex + 1),
					FText::AsNumber(GetFrameCount()));
			})
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SAssignNew(PreviewCanvasWidget, SFrameEventPreviewCanvas)
			.Flipbook_Lambda([this]() -> UPaperFlipbook* { return GetPreviewFlipbook(); })
			.FrameIndex_Lambda([this]() { return PreviewDisplayFrameIndex; })
			.Asset_Lambda([this]()
			{
				return Model.IsValid()
					? TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset>(Model->GetAsset())
					: TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset>();
			})
			.LayerAsset(Cast<UPaper2DPlusCharacterLayerAsset>(
				DataProvider->GetTransactionTarget()))
			.Model(Model)
			.Cues_Lambda([this]() { return GetFrameCues(); })
			.FlipbookIndex_Lambda([this]() { return SelectedFlipbookIndex; })
			.SelectedEventIndex_Lambda([this]() { return SelectedEventIndex; })
			.PreviewHost(PreviewHost.Get())
			.OnCueOffsetDragStarted(this, &SFrameEventEditor::OnPreviewCueOffsetDragStarted)
			.OnCueOffsetChanged(this, &SFrameEventEditor::OnPreviewCueOffsetChanged)
			.OnCueOffsetDragEnded(this, &SFrameEventEditor::OnPreviewCueOffsetDragEnded)
			.AccessibleText_Lambda([this]()
			{
				return PreviewCanvasWidget.IsValid()
					? PreviewCanvasWidget->GetAccessibleSummaryText()
					: LOCTEXT("FrameCuePreviewPending", "Frame Cue preview canvas");
			})
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			BuildUnifiedTimeline()
		];
}

TSharedRef<SWidget> SFrameEventEditor::BuildCueDetailsPanel()
{
	if (EventDetailsView.IsValid())
	{
		EventDetailsView->SetObject(nullptr);
		EventDetailsView.Reset();
	}
	DetailsCueSubject.Reset();
	SelectedCurveRenameEditor.Reset();
	CurveDetailsSelection = NAME_None;
	CurveRenameSubject = NAME_None;
	CurveRenameScope = {};
	CurveDetailsError = FText::GetEmpty();
	FPropertyEditorModule& PropertyModule =
		FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
	FDetailsViewArgs DetailsArgs;
	DetailsArgs.bAllowSearch = false;
	DetailsArgs.bHideSelectionTip = true;
	DetailsArgs.bShowOptions = false;
	DetailsArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	DetailsArgs.NotifyHook = this;
	EventDetailsView = PropertyModule.CreateDetailView(DetailsArgs);
	EventDetailsView->SetIsPropertyVisibleDelegate(
		FIsPropertyVisible::CreateSP(this, &SFrameEventEditor::IsCuePlacementPropertyVisible));
	EventDetailsView->OnFinishedChangingProperties().AddSP(
		this, &SFrameEventEditor::HandleEventDetailsFinishedChangingProperties);

	TSharedRef<SWidget> CueActions = FProfilePropertyRowUtils::MakeRow(
		LOCTEXT("SelectedCueActionsLabel", "Actions"),
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0.0f, 0.0f, 4.0f, 0.0f)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.IsEnabled_Lambda([this]()
			{
				return IsCuePrimarySelection()
					&& CanMutateLiveSelection();
			})
			.OnClicked_Lambda([this]()
			{
				DuplicateEvent(SelectedEventIndex);
				return FReply::Handled();
			})
			[
				SNew(STextBlock).Text(LOCTEXT("DetailsDuplicateFrameCue", "Duplicate"))
			]
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0.0f, 0.0f, 4.0f, 0.0f)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.Visibility_Lambda([this]()
			{
				return IsCuePrimarySelection() && CanEditSelectedCueType()
					? EVisibility::Visible
					: EVisibility::Collapsed;
			})
			.IsEnabled_Lambda([this]() { return CanEditSelectedCueType(); })
			.OnClicked_Lambda([this]()
			{
				EditSelectedCueType();
				return FReply::Handled();
			})
			[
				SNew(STextBlock).Text(LOCTEXT("DetailsEditFrameCueType", "Edit Cue Type"))
			]
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.IsEnabled_Lambda([this]()
			{
				return IsCuePrimarySelection()
					&& CanMutateLiveSelection();
			})
			.OnClicked_Lambda([this]()
			{
				RemoveEvent(SelectedEventIndex);
				return FReply::Handled();
			})
			[
				SNew(STextBlock).Text(LOCTEXT("DetailsRemoveFrameCue", "Remove"))
			]
		],
		LOCTEXT(
			"SelectedCueActionsTip",
			"These actions apply to the Cue selected directly on the timeline."));

	TSharedRef<SVerticalBox> CurveDetails = SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			FProfilePropertyRowUtils::MakeRow(
				LOCTEXT("SelectedCurveNameLabel", "Curve"),
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 6.0f, 0.0f)
				[
					SNew(SColorBlock)
					.Color_Lambda([this]()
					{
						return Paper2DPlusCurveTracks::NameToColor(GetSelectedCurveName());
					})
					.Size(FVector2D(12.0f, 12.0f))
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text_Lambda([this]() { return FText::FromName(GetSelectedCurveName()); })
					.Font(FProfilePropertyRowUtils::GetPropertyFont())
				],
				LOCTEXT("SelectedCurveNameTip", "The curve selected in the timeline legend."))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			FProfilePropertyRowUtils::MakeRow(
				LOCTEXT("SelectedCurvePresentationLabel", "Graph"),
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 4.0f, 0.0f)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
					.IsEnabled_Lambda([this]()
					{
						return CurveStack.IsValid() && !GetSelectedCurveName().IsNone();
					})
					.OnClicked_Lambda([this]()
					{
						if (CurveStack.IsValid())
						{
							CurveStack->ToggleCurveVisibility(GetSelectedCurveName());
							RefreshDetailsPanel();
						}
						return FReply::Handled();
					})
					[
						SNew(STextBlock)
						.Text_Lambda([this]()
						{
							return CurveStack.IsValid()
								&& CurveStack->IsCurveBaseVisible(GetSelectedCurveName())
									? LOCTEXT("SelectedCurveVisible", "Visible")
									: LOCTEXT("SelectedCurveHidden", "Hidden");
						})
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
					.IsEnabled_Lambda([this]()
					{
						return CurveStack.IsValid() && !GetSelectedCurveName().IsNone();
					})
					.OnClicked_Lambda([this]()
					{
						if (CurveStack.IsValid())
						{
							CurveStack->ToggleCurveSolo(GetSelectedCurveName());
							RefreshDetailsPanel();
						}
						return FReply::Handled();
					})
					[
						SNew(STextBlock)
						.Text_Lambda([this]()
						{
							return CurveStack.IsValid()
								&& CurveStack->IsCurveSoloed(GetSelectedCurveName())
									? LOCTEXT("SelectedCurveSoloOn", "Solo: On")
									: LOCTEXT("SelectedCurveSoloOff", "Solo: Off");
						})
					]
				],
				LOCTEXT(
					"SelectedCurvePresentationTip",
					"Visibility and solo are editor-only graph presentation; they never dirty the asset."))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			FProfilePropertyRowUtils::MakeRow(
				LOCTEXT("SelectedCurveModeLabel", "Interpolation"),
				SNew(SComboButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.IsEnabled_Lambda([this]()
				{
					return CurveStack.IsValid()
						&& CurveStack->CanMutateCurve(GetSelectedCurveName());
				})
				.OnGetMenuContent(this, &SFrameEventEditor::BuildSelectedCurveModeMenu)
				.ButtonContent()
				[
					SNew(STextBlock)
					.Text(this, &SFrameEventEditor::GetSelectedCurveModeText)
					.Font(FProfilePropertyRowUtils::GetPropertyFont())
				],
				LOCTEXT(
					"SelectedCurveModeTip",
					"Set interpolation for every key on the selected curve."))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			FProfilePropertyRowUtils::MakeRow(
				LOCTEXT("SelectedCurveRenameLabel", "Name"),
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SAssignNew(SelectedCurveRenameEditor, SEditableTextBox)
					.SelectAllTextWhenFocused(true)
					.IsEnabled_Lambda([this]()
					{
						return CurveStack.IsValid()
							&& CurveStack->CanMutateCurve(GetSelectedCurveName());
					})
					.OnTextCommitted(
						this, &SFrameEventEditor::CommitSelectedCurveRename)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
					.IsEnabled_Lambda([this]()
					{
						return SelectedCurveRenameEditor.IsValid()
							&& CurveStack.IsValid()
							&& CurveStack->CanMutateCurve(GetSelectedCurveName());
					})
					.OnClicked_Lambda([this]()
					{
						if (SelectedCurveRenameEditor.IsValid())
						{
							CommitSelectedCurveRename(
								SelectedCurveRenameEditor->GetText(),
								ETextCommit::OnEnter);
						}
						return FReply::Handled();
					})
					[
						SNew(STextBlock).Text(LOCTEXT("ApplySelectedCurveRename", "Rename"))
					]
				],
				LOCTEXT(
					"SelectedCurveRenameTip",
					"Rename the selected curve in one transaction."))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			FProfilePropertyRowUtils::MakeRow(
				LOCTEXT("SelectedCurveKeysLabel", "Keys"),
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(this, &SFrameEventEditor::GetSelectedCurveOrphanText)
					.AutoWrapText(true)
					.ColorAndOpacity_Lambda([this]()
					{
						return CurveDetailsError.IsEmpty()
							? FSlateColor::UseSubduedForeground()
							: FSlateColor(FLinearColor(0.9f, 0.25f, 0.2f));
					})
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Left)
				.Padding(0.0f, 4.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
					.Visibility_Lambda([this]()
					{
						return CurveStack.IsValid()
							&& CurveStack->HasCurveOrphans(GetSelectedCurveName())
								? EVisibility::Visible
								: EVisibility::Collapsed;
					})
					.IsEnabled_Lambda([this]()
					{
						return CurveStack.IsValid()
							&& CurveStack->HasResolvedCurveTiming()
							&& CurveStack->CanMutateCurve(GetSelectedCurveName())
							&& CurveStack->HasCurveOrphans(GetSelectedCurveName());
					})
					.OnClicked_Lambda([this]()
					{
						if (CurveStack.IsValid())
						{
							CurveStack->PruneOrphanKeys(GetSelectedCurveName());
							RefreshDetailsPanel();
						}
						return FReply::Handled();
					})
					[
						SNew(STextBlock).Text(LOCTEXT("PruneSelectedCurveOrphans", "Prune orphan keys"))
					]
				],
				LOCTEXT(
					"SelectedCurveKeysTip",
					"Keys outside the animation's frame range are preserved until explicitly pruned."))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			FProfilePropertyRowUtils::MakeRow(
				LOCTEXT("SelectedCurveRemoveLabel", "Curve"),
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.IsEnabled_Lambda([this]()
				{
					return CurveStack.IsValid()
						&& CurveStack->CanMutateCurve(GetSelectedCurveName());
				})
				.OnClicked_Lambda([this]()
				{
					if (CurveStack.IsValid())
					{
						CurveStack->RemoveCurve(GetSelectedCurveName());
						RefreshDetailsPanel();
					}
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text(LOCTEXT("RemoveSelectedCurve", "Remove curve"))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.9f, 0.35f, 0.3f)))
				],
				LOCTEXT(
					"RemoveSelectedCurveTip",
					"Remove the selected curve and all of its keys from this animation."))
		];

	TSharedRef<SWidget> Panel = SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 6.0f, 8.0f, 4.0f)
		[
			SNew(STextBlock)
			.Text(this, &SFrameEventEditor::GetTimelineDetailsSummary)
			.AutoWrapText(true)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBox)
			.Visibility_Lambda([this]()
			{
				return IsCuePrimarySelection()
					? EVisibility::Visible
					: EVisibility::Collapsed;
			})
			[
				CueActions
			]
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SBox)
			.Visibility_Lambda([this]()
			{
				return IsCuePrimarySelection()
					? EVisibility::Visible
					: EVisibility::Collapsed;
			})
			[
				EventDetailsView.ToSharedRef()
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBox)
			.Visibility_Lambda([this]()
			{
				return IsCurvePrimarySelection()
					? EVisibility::Visible
					: EVisibility::Collapsed;
			})
			[
				CurveDetails
			]
		];
	DetailsPanelWidget = Panel;
	RefreshDetailsPanel();
	return Panel;
}

TSharedRef<SWidget> SFrameEventEditor::BuildSelectedCurveModeMenu()
{
	const TWeakPtr<SFrameEventEditor> WeakSelf = SharedThis(this);
	FMenuBuilder Menu(/*bInShouldCloseWindowAfterMenuSelection=*/ true, nullptr);
	Menu.BeginSection(
		TEXT("SelectedCurveInterpolation"),
		LOCTEXT("SelectedCurveInterpolationSection", "Interpolation"));
	const auto AddMode = [&Menu, WeakSelf](
		const EPaper2DPlusCurveInterp Mode,
		const FText& ToolTip)
	{
		Menu.AddMenuEntry(
			FrameEventEditorInternal::CurveModeToText(Mode),
			ToolTip,
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([WeakSelf, Mode]()
				{
					if (const TSharedPtr<SFrameEventEditor> Editor = WeakSelf.Pin();
						Editor.IsValid() && Editor->CurveStack.IsValid())
					{
						const FName CurveName = Editor->GetSelectedCurveName();
						Editor->CurveStack->SetCurveMode(CurveName, Mode);
						Editor->RefreshDetailsPanel();
					}
				}),
				FCanExecuteAction::CreateLambda([WeakSelf]()
				{
					const TSharedPtr<SFrameEventEditor> Editor = WeakSelf.Pin();
					return Editor.IsValid()
						&& Editor->CurveStack.IsValid()
						&& Editor->CurveStack->CanMutateCurve(
							Editor->GetSelectedCurveName());
				}),
				FIsActionChecked::CreateLambda([WeakSelf, Mode]()
				{
					const TSharedPtr<SFrameEventEditor> Editor = WeakSelf.Pin();
					return Editor.IsValid()
						&& Editor->CurveStack.IsValid()
						&& Editor->CurveStack->GetCurveMode(
							Editor->GetSelectedCurveName()) == Mode;
				})),
			NAME_None,
			EUserInterfaceActionType::RadioButton);
	};
	AddMode(
		EPaper2DPlusCurveInterp::Constant,
		LOCTEXT("CurveModeConstantTip", "Hold each key's value until the next key."));
	AddMode(
		EPaper2DPlusCurveInterp::Linear,
		LOCTEXT("CurveModeLinearTip", "Interpolate linearly between keys."));
	AddMode(
		EPaper2DPlusCurveInterp::Cubic,
		LOCTEXT("CurveModeCubicTip", "Use cubic interpolation between keys."));
	Menu.EndSection();
	return Menu.MakeWidget();
}

// Secondary Cue concepts — net policy, Range end reasons — are worded in the pure tab-presentation
// rules and surfaced HERE, in the details pane, so the timeline chrome stays pick-frame/add-cue/preview.
FText SFrameEventEditor::GetTimelineDetailsSummary() const
{
	using namespace Paper2DPlusFrameCueTabPresentation;
	if (!UnifiedTimeline.IsValid())
	{
		return BuildEmptyDetailsSummary();
	}
	const Paper2DPlusFrameCueTimeline::FPrimarySelection& Selection =
		UnifiedTimeline->GetPrimarySelection();
	switch (Selection.Kind)
	{
	case Paper2DPlusFrameCueTimeline::EPrimarySelectionKind::Track:
		return BuildTrackDetailsSummary(Selection.TrackId.IsValid());
	case Paper2DPlusFrameCueTimeline::EPrimarySelectionKind::Curve:
		return BuildCurveDetailsSummary(
			Selection.CurveName, DataProvider.IsValid() && DataProvider->SupportsCurveEditing());
	case Paper2DPlusFrameCueTimeline::EPrimarySelectionKind::Cue:
		return BuildPlacementDetailsSummary(Selection.Cue.Get());
	case Paper2DPlusFrameCueTimeline::EPrimarySelectionKind::None:
	default:
		return BuildEmptyDetailsSummary();
	}
}

FName SFrameEventEditor::GetSelectedCurveName() const
{
	if (!UnifiedTimeline.IsValid())
	{
		return NAME_None;
	}
	const Paper2DPlusFrameCueTimeline::FPrimarySelection& Selection =
		UnifiedTimeline->GetPrimarySelection();
	return Selection.Kind == Paper2DPlusFrameCueTimeline::EPrimarySelectionKind::Curve
		? Selection.CurveName
		: NAME_None;
}

bool SFrameEventEditor::IsCuePrimarySelection() const
{
	return UnifiedTimeline.IsValid()
		&& UnifiedTimeline->GetPrimarySelection().Kind
			== Paper2DPlusFrameCueTimeline::EPrimarySelectionKind::Cue
		&& ResolveCueIdentity(SelectedCueIdentity) != nullptr;
}

bool SFrameEventEditor::IsCurvePrimarySelection() const
{
	const FName CurveName = GetSelectedCurveName();
	return CurveStack.IsValid()
		&& !CurveName.IsNone()
		&& CurveStack->GetSelectedCurve() == CurveName;
}

FText SFrameEventEditor::GetSelectedCurveModeText() const
{
	return CurveStack.IsValid() && !GetSelectedCurveName().IsNone()
		? FrameEventEditorInternal::CurveModeToText(
			CurveStack->GetCurveMode(GetSelectedCurveName()))
		: FText::GetEmpty();
}

FText SFrameEventEditor::GetSelectedCurveOrphanText() const
{
	if (!CurveDetailsError.IsEmpty())
	{
		return CurveDetailsError;
	}
	if (!CurveStack.IsValid() || GetSelectedCurveName().IsNone())
	{
		return LOCTEXT("NoSelectedCurveKeys", "Select a curve in the timeline.");
	}
	if (!CurveStack->HasResolvedCurveTiming())
	{
		return LOCTEXT(
			"SelectedCurveTimingUnavailable",
			"Animation timing is unavailable; orphan-key state is unknown.");
	}

	const TArray<int32> Orphans =
		CurveStack->GetCurveOrphanFrames(GetSelectedCurveName());
	if (Orphans.IsEmpty())
	{
		return LOCTEXT(
			"SelectedCurveNoOrphans",
			"No orphan keys. Drag keys in the graph; Shift+Click adds and Delete removes.");
	}

	const FString FrameList = FString::JoinBy(
		Orphans,
		TEXT(", "),
		[](const int32 Frame) { return FString::FromInt(Frame); });
	return FText::Format(
		LOCTEXT(
			"SelectedCurveOrphansFmt",
			"{0} orphan key(s) outside this animation's frames: {1}."),
		FText::AsNumber(Orphans.Num()),
		FText::FromString(FrameList));
}

FProfileScopedAnimationIdentity SFrameEventEditor::GetCurrentCurveRenameScope() const
{
	return DataProvider.IsValid()
		? DataProvider->GetScopedAnimationIdentity(SelectedFlipbookIndex)
		: FProfileScopedAnimationIdentity();
}

bool SFrameEventEditor::IsCurveRenameScopeCurrent() const
{
	const FProfileScopedAnimationIdentity CurrentScope = GetCurrentCurveRenameScope();
	return CurveRenameScope.IsValid()
		&& CurrentScope.IsValid()
		&& CurveRenameScope.Animation == CurrentScope.Animation
		&& CurveRenameScope.LayerScope == CurrentScope.LayerScope;
}

void SFrameEventEditor::ResetCurveRenameEditorToSelection()
{
	CurveRenameSubject = GetSelectedCurveName();
	CurveRenameScope = CurveRenameSubject.IsNone()
		? FProfileScopedAnimationIdentity()
		: GetCurrentCurveRenameScope();
	CurveDetailsError = FText::GetEmpty();
	if (SelectedCurveRenameEditor.IsValid())
	{
		SelectedCurveRenameEditor->SetError(FText::GetEmpty());
		SelectedCurveRenameEditor->SetText(FText::FromName(CurveRenameSubject));
	}
}

void SFrameEventEditor::SettleCurveRenameBeforeScopeChange()
{
	if (SelectedCurveRenameEditor.IsValid()
		&& SelectedCurveRenameEditor->HasKeyboardFocus()
		&& !CurveRenameSubject.IsNone())
	{
		CommitSelectedCurveRename(
			SelectedCurveRenameEditor->GetText(),
			ETextCommit::OnUserMovedFocus);
	}

	// Whether the pending text committed or failed validation, it must never survive into another
	// Profile/Layer/animation scope. RefreshAll seeds the destination selection after the retarget.
	CurveRenameSubject = NAME_None;
	CurveRenameScope = {};
	CurveDetailsError = FText::GetEmpty();
	if (SelectedCurveRenameEditor.IsValid())
	{
		SelectedCurveRenameEditor->SetError(FText::GetEmpty());
		SelectedCurveRenameEditor->SetText(FText::GetEmpty());
	}
}

void SFrameEventEditor::CommitSelectedCurveRename(
	const FText& NewText,
	const ETextCommit::Type CommitType)
{
	if (!SelectedCurveRenameEditor.IsValid())
	{
		return;
	}
	const FName CurveName = CurveRenameSubject.IsNone()
		? GetSelectedCurveName()
		: CurveRenameSubject;
	if (!CurveStack.IsValid()
		|| CurveName.IsNone()
		|| !IsCurveRenameScopeCurrent()
		|| !CurveStack->CanMutateCurve(CurveName))
	{
		ResetCurveRenameEditorToSelection();
		return;
	}
	if (CommitType == ETextCommit::OnCleared)
	{
		ResetCurveRenameEditorToSelection();
		return;
	}

	const FString RequestedName = NewText.ToString().TrimStartAndEnd();
	if (RequestedName.Equals(CurveName.ToString(), ESearchCase::CaseSensitive))
	{
		ResetCurveRenameEditorToSelection();
		return;
	}

	FText Error;
	if (!CurveStack->RenameCurve(CurveName, RequestedName, &Error))
	{
		CurveDetailsError = Error;
		SelectedCurveRenameEditor->SetError(Error);
		Invalidate(EInvalidateWidgetReason::Paint);
		return;
	}

	CurveDetailsError = FText::GetEmpty();
	SelectedCurveRenameEditor->SetError(FText::GetEmpty());
	RefreshDetailsPanel();
	ResetCurveRenameEditorToSelection();
}

bool SFrameEventEditor::IsCuePlacementPropertyVisible(const FPropertyAndParent& PropertyAndParent) const
{
	// One body, two callers, no duplicated filter to drift. The delegation runs this way round because
	// FPropertyAndParent cannot be synthesized: on 5.0-5.8 its only constructors take a live
	// IPropertyHandle or FPropertyNode, which a headless test has no way to produce. So the bound
	// delegate unwraps to the bare FProperty and calls the same function the test calls — the test is
	// therefore asking the shipped filter, not a copy of it.
	return IsCuePlacementPropertyVisibleForTests(&PropertyAndParent.Property);
}

bool SFrameEventEditor::IsCuePlacementPropertyVisibleForTests(const FProperty* Property) const
{
	return Paper2DPlusFrameCueTabPresentation::IsPlacementDetailsPropertyVisible(
		Property, ResolveCueIdentity(SelectedCueIdentity));
}

TSharedRef<SWidget> SFrameEventEditor::BuildPreviewPanel()
{
	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(8, 6, 8, 2)
		[
			SNew(STextBlock)
			.Text_Lambda([this]()
			{
				return SFrameEventPreviewCanvas::GetPreviewStatusText(
					ResolveCueIdentity(SelectedCueIdentity),
					PreviewHost ? PreviewHost->GetContext() : nullptr,
					PreviewHost ? PreviewHost->GetOwnedResourceCount() : INDEX_NONE);
			})
			.AutoWrapText(true)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(8, 0, 8, 6)
		[
			SNew(STextBlock)
			.Text_Lambda([this]()
			{
				return FText::Format(
					LOCTEXT("PreviewResourceCountFmt", "Owned preview resources: {0}"),
					FText::AsNumber(GetPreviewResourceCountForTests()));
			})
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(8, 0, 8, 6)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
			[
				SNew(SButton)
				.IsEnabled_Lambda([this]()
				{
					return bHostActive && ResolveCueIdentity(SelectedCueIdentity) != nullptr;
				})
				.OnClicked_Lambda([this]()
				{
					PreviewSelectedCue();
					return FReply::Handled();
				})
				[
					SNew(STextBlock).Text(LOCTEXT("PreviewSelectedCueAction", "Preview Selected"))
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
			[
				SNew(SButton)
				.IsEnabled_Lambda([this]() { return bHostActive && PreviewHost != nullptr; })
				.OnClicked_Lambda([this]()
				{
					StopPlayback();
					return FReply::Handled();
				})
				[
					SNew(STextBlock).Text(LOCTEXT("ClearCuePreviewAction", "Clear"))
				]
			]
		];
	PreviewPanelWidget = Panel;
	return Panel;
}

TSharedRef<SWidget> SFrameEventEditor::BuildCurveTrackContent()
{
	if (CurveStack.IsValid())
	{
		CurveStack->HandleHostDeactivated();
		CurveStack.Reset();
	}
	if (!DataProvider.IsValid())
	{
		return SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
			.Padding(FMargin(8.0f, 6.0f))
			[
				SNew(STextBlock)
				.Text(LOCTEXT(
					"CurveContextUnavailable",
					"Curve data is unavailable for this animation context."))
				.AutoWrapText(true)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			];
	}

	return SAssignNew(CurveStack, SCurveTrackStack)
		.Asset_Lambda([this]()
		{
			return Model.IsValid()
				? TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset>(Model->GetAsset())
				: TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset>();
		})
		.SelectedFlipbookIndex_Lambda([this]() { return SelectedFlipbookIndex; })
		.SelectedFrameIndex_Lambda([this]() { return SelectedFrameIndex; })
		.CanEditCurves_Lambda([this]()
		{
			return DataProvider.IsValid() && DataProvider->SupportsCurveEditing();
		})
		.OnCurveListChanged_Lambda([this]()
		{
			ReconcileTimelineCurveSelection();
			if (Model.IsValid())
			{
				Model->NotifyAssetDataChanged();
			}
		})
		.OnGestureSettled_Lambda([this]()
		{
			if (bHostActive && bNeedsRefresh && !HasActiveTransaction())
			{
				RefreshAll();
			}
		})
		.OnCurveSelectionChanged_Lambda([this](const FName CurveName)
		{
			if (UnifiedTimeline.IsValid())
			{
				UnifiedTimeline->HandleCurveSelected(CurveName);
			}
		});
}

void SFrameEventEditor::ReconcileTimelineCurveSelection()
{
	if (!UnifiedTimeline.IsValid() || !CurveStack.IsValid())
	{
		return;
	}
	const TSet<FName> AuthoredCurveNames(CurveStack->GetAuthoredCurveNames());
	UnifiedTimeline->ReconcileCurveSelection(AuthoredCurveNames);
	const Paper2DPlusFrameCueTimeline::FPrimarySelection& Selection =
		UnifiedTimeline->GetPrimarySelection();
	CurveStack->SynchronizeSelection(
		Selection.Kind == Paper2DPlusFrameCueTimeline::EPrimarySelectionKind::Curve
			? Selection.CurveName
			: NAME_None);
	RefreshDetailsPanel();
}

// ─── FLIPBOOK LIST ───────────────────────────────────────────────────────────

void SFrameEventEditor::RefreshFlipbookList()
{
	UPaper2DPlusCharacterProfileAsset* AssetPtr = Model.IsValid() ? Model->GetAsset() : nullptr;
	if (!FlipbookListBox.IsValid() || !AssetPtr) return;
	FlipbookListBox->ClearChildren();

	const FString& SearchFilter = Model->GetFlipbookGroupSearchText();

	auto ItemBuilder = [this, AssetPtr](int32 i) -> TSharedRef<SWidget>
	{
		const FFlipbookProfileEntry& Anim = AssetPtr->Flipbooks[i];
		UPaperFlipbook* LoadedFlipbook = Anim.Identity.Flipbook.Get();
		const TArray<TObjectPtr<UPaper2DPlusCueBase>>* ProviderCues =
			DataProvider.IsValid() ? DataProvider->GetCues(i) : nullptr;
		const int32 EventCount = ProviderCues ? ProviderCues->Num() : 0;
		const FProfileAnimationIdentity AnimationIdentity = DataProvider.IsValid()
			? DataProvider->GetAnimationIdentity(i)
			: FProfileAnimationIdentity();

		return SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "NoBorder")
			.OnClicked_Lambda([this, AnimationIdentity]()
			{
				const int32 CurrentIndex = DataProvider.IsValid()
					? DataProvider->ResolveAnimationIndex(AnimationIdentity)
					: INDEX_NONE;
				if (Model.IsValid() && CurrentIndex != INDEX_NONE)
				{
					Model->SetSelectedFlipbook(CurrentIndex);
				}
				return FReply::Handled();
			})
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
				.BorderBackgroundColor_Lambda([this, i]()
				{
					return i == SelectedFlipbookIndex
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
							.Visibility(EventCount > 0 ? EVisibility::Visible : EVisibility::Collapsed)
							.Text(FText::Format(LOCTEXT("CueCountBadge", "{0} cue(s)"), FText::AsNumber(EventCount)))
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
							.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
						]
					]
				]
			];
	};

	TFunction<bool(int32)> FilterFn = nullptr;
	if (!SearchFilter.IsEmpty())
	{
		FilterFn = [AssetPtr, &SearchFilter](int32 Idx) -> bool
		{
			return AssetPtr->Flipbooks[Idx].Identity.FlipbookName.Contains(SearchFilter, ESearchCase::IgnoreCase);
		};
	}

	FFlipbookListBuilder::Build(FlipbookListBox, Model, ItemBuilder, [this]() { RefreshFlipbookList(); }, FilterFn);
}

// ─── FRAME STRIP ─────────────────────────────────────────────────────────────

void SFrameEventEditor::RefreshFrameStrip()
{
	if (!FrameStripBox.IsValid()) return;
	FrameStripBox->ClearChildren();

	// The strip follows the directional preview like the canvas directly above it (Sprite, Hitbox
	// and Root Motion already do), falling back to base rows when the bearing has no art so the
	// frame ruler never disappears. Frame counts are identical by the compatibility gate.
	UPaperFlipbook* PreviewFB = GetPreviewFlipbook();
	UPaperFlipbook* FB = PreviewFB ? PreviewFB : GetSelectedFlipbook();
	if (!FB || FB->GetNumKeyFrames() == 0) return;

	for (int32 i = 0; i < FB->GetNumKeyFrames(); i++)
	{
		UPaperSprite* Sprite = PreviewFB
			? PreviewFB->GetKeyFrameChecked(i).Sprite
			: nullptr;

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
			TArray<TObjectPtr<UPaper2DPlusCueBase>>* Cues = GetFrameCuesArray();
			if (!Cues) return FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);

			for (const TObjectPtr<UPaper2DPlusCueBase>& Cue : *Cues)
			{
				if (!Cue) continue;

				if (const UPaper2DPlusCue* Moment = Cast<UPaper2DPlusCue>(Cue))
				{
					if (Moment->TriggerFrame == i)
					{
						return FLinearColor(0.3f, 0.8f, 0.3f); // Green
					}
				}

				if (const UPaper2DPlusCueState* Ranged = Cast<UPaper2DPlusCueState>(Cue))
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

// ─── DETAILS PANEL ──────────────────────────────────────────────────────────

void SFrameEventEditor::RefreshDetailsPanel()
{
	UPaper2DPlusCueBase* CueSubject = IsCuePrimarySelection()
		? ResolveCueIdentity(SelectedCueIdentity)
		: nullptr;
	if (EventDetailsView.IsValid())
	{
		if (DetailsCueSubject.Get() != CueSubject)
		{
			DetailsCueSubject = CueSubject;
			EventDetailsView->SetObject(CueSubject);
		}
	}

	const FName SelectedCurve = GetSelectedCurveName();
	if (SelectedCurve != CurveDetailsSelection)
	{
		CurveDetailsSelection = SelectedCurve;
		ResetCurveRenameEditorToSelection();
	}
	else if (SelectedCurveRenameEditor.IsValid()
		&& !SelectedCurveRenameEditor->HasKeyboardFocus())
	{
		CurveRenameSubject = SelectedCurve;
		CurveRenameScope = SelectedCurve.IsNone()
			? FProfileScopedAnimationIdentity()
			: GetCurrentCurveRenameScope();
		const FText CurveNameText = FText::FromName(SelectedCurve);
		if (!SelectedCurveRenameEditor->GetText().EqualTo(CurveNameText))
		{
			SelectedCurveRenameEditor->SetText(CurveNameText);
		}
	}

	if (const TSharedPtr<SWidget> Panel = DetailsPanelWidget.Pin())
	{
		Panel->Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
	}
}

void SFrameEventEditor::InvalidatePreviewPanel()
{
	if (const TSharedPtr<SWidget> Panel = PreviewPanelWidget.Pin())
	{
		Panel->Invalidate(EInvalidateWidgetReason::Paint);
	}
}

void SFrameEventEditor::RefreshAll()
{
	if (!bHostActive)
	{
		bNeedsRefresh = true;
		return;
	}
	if (bEventDetailsPropertyChangeActive)
	{
		bNeedsRefresh = true;
		return; // never rebuild the details view underneath its live PropertyEditor gesture
	}
	bNeedsRefresh = false;
	ReconcileSelectedCueIdentity();
	// This is the only synchronous load boundary for Cue-owned soft flipbook art in the panel. It runs
	// before any Slate row/layout/paint work, which remains strictly resident-only.
	WarmCurrentAnimationCueEffects();
	if (PreviewCanvasWidget.IsValid()) PreviewCanvasWidget->ResetCachedGeometry();
	RefreshFrameStrip();
	RefreshDetailsPanel();
	if (TimelineTrack.IsValid()) TimelineTrack->Invalidate(EInvalidateWidgetReason::Layout);
	if (UnifiedTimeline.IsValid())
	{
		UnifiedTimeline->RefreshTrackHeaders();
		if (SelectedEventIndex != INDEX_NONE)
		{
			UnifiedTimeline->HandleCueSelected(SelectedEventIndex);
		}
	}
	// Structural-only: rebuilds rows when the curve SET changed (flipbook switch / profile swap /
	// curve add+remove / undo), else just repaints — never a wholesale rebuild for value edits.
	if (CurveStack.IsValid())
	{
		CurveStack->RefreshTracks();
		ReconcileTimelineCurveSelection();
	}
}

// ─── PLAYBACK ────────────────────────────────────────────────────────────────

void SFrameEventEditor::TogglePlayback()
{
	if (!bHostActive)
	{
		return;
	}
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
	if (!Decision.bShouldBePlaying)
	{
		StopPlayback();
		bResumeAfterDirectionalPreviewResolves =
			Decision.bResumeAfterDirectionalPreviewResolves;
		return;
	}

	bResumeAfterDirectionalPreviewResolves =
		Decision.bResumeAfterDirectionalPreviewResolves;
	if (Model.IsValid()
		&& Model->IsDirectionalPreviewEnabled()
		&& !Model->IsDirectionalPreviewRenderable()) return;
	UPaperFlipbook* FB = GetSelectedFlipbook();
	if (!FB || FB->GetNumKeyFrames() == 0) return;

	// Inspection/scrub previews and playback are separate sessions. End any active range and reset
	// its host resources, then seed playback from a clean lifecycle state.
	ForceEndAllActiveRangedEvents(EPaper2DPlusFrameCueEndReason::EditorReset);
	LastPreviewFrame = INDEX_NONE;
	PreviewEvaluationMode = EPaper2DPlusFrameCueEvaluationMode::EditorPlayback;
	bIsPlaying = true;
	bResumeAfterDirectionalPreviewResolves = false;
	PreviewDisplayFrameIndex = SelectedFrameIndex;

	CachedTiming = FFlipbookTimingData::ReadFromFlipbook(FB);
	PlaybackTime = CachedTiming.GetFrameStartTime(SelectedFrameIndex);

	PlaybackTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateSP(this, &SFrameEventEditor::OnPlaybackTick), 1.0f / 60.0f);
}

void SFrameEventEditor::StopPlayback()
{
	const bool bStoppingPlaybackSession =
		bIsPlaying
		|| PreviewEvaluationMode == EPaper2DPlusFrameCueEvaluationMode::EditorPlayback;
	StopPlaybackForBoundary(
		bStoppingPlaybackSession
			? EPaper2DPlusFrameCueEndReason::PlaybackStopped
			: EPaper2DPlusFrameCueEndReason::EditorReset);
}

void SFrameEventEditor::StopPlaybackForBoundary(
	const EPaper2DPlusFrameCueEndReason EndReason)
{
	bResumeAfterDirectionalPreviewResolves = false;
	StopPreviewResourceTicker();
	StopPlaybackTicker();
	// Deliver any outstanding Range End first, then let the host enforce a zero-resource ledger even
	// if an adapter forgot to release its handles. Callers name the real boundary so a tool reset,
	// mutation, or animation switch cannot be mislabeled as a manual playback stop.
	ForceEndAllActiveRangedEvents(EndReason);
	LastPreviewFrame = INDEX_NONE;
	PreviewDisplayFrameIndex = SelectedFrameIndex;
	InvalidatePreviewPanel();
}

void SFrameEventEditor::StopPlaybackTicker()
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

bool SFrameEventEditor::OnPlaybackTick(float /*DeltaTime*/)
{
	if (!bIsPlaying || !bHostActive)
	{
		PlaybackTickerHandle.Reset();
		return false;
	}

	const float ActualDelta = FApp::GetDeltaTime();
	PlaybackTime += ActualDelta;

	// Loop wrap detection. Keep the preview world and active ledger alive so shared dispatch can apply
	// the runtime loop-seam rule: a Cue State that still covers the wrapped frame stays continuous
	// (optional Update only), while one that no longer covers it receives LoopReset End. Do NOT reset
	// LastPreviewFrame here or the wrap transition would be lost.
	if (CachedTiming.TotalDurationSeconds > 0.0f && PlaybackTime >= CachedTiming.TotalDurationSeconds)
	{
		PlaybackTime = FMath::Fmod(PlaybackTime, CachedTiming.TotalDurationSeconds);
	}

	int32 NewFrame = GetFrameFromTime();
	if (NewFrame != SelectedFrameIndex)
	{
		if (Model.IsValid()) Model->SetSelectedFrame(NewFrame);
		else
		{
			SelectedFrameIndex = NewFrame;
			PreviewDisplayFrameIndex = NewFrame;
		}
		if (FrameStripBox.IsValid()) FrameStripBox->Invalidate(EInvalidateWidgetReason::Paint);
		if (TimelineTrack.IsValid()) TimelineTrack->Invalidate(EInvalidateWidgetReason::Paint);
		if (CurveStack.IsValid()) CurveStack->InvalidateScrub();
	}

	// Fire frame events for this transition through the SAME shared dispatch the runtime uses
	// (one-shot + ranged Begin/Tick/End + custom BP), so state and custom events come alive in the
	// editor preview instead of looking dead. bFromPlayback=true so a genuine loop wrap (frame went
	// backward because playback looped past the last frame) is honored as a wrap — only manual
	// backward scrubs are reinterpreted as a fresh seek.
	if (NewFrame != LastPreviewFrame)
	{
		DispatchPreviewTransition(NewFrame, /*bFromPlayback*/ true);
	}

	Invalidate(EInvalidateWidgetReason::Paint);
	return true;
}

bool SFrameEventEditor::OnPreviewResourceTick(float /*DeltaTime*/)
{
	if (bIsPlaying || !PreviewHost)
	{
		PreviewResourceTickerHandle.Reset();
		return false;
	}

	const int32 ResourceCount = PreviewHost->GetOwnedResourceCount();
	if (ResourceCount != LastReportedPreviewResourceCount)
	{
		LastReportedPreviewResourceCount = ResourceCount;
		InvalidatePreviewPanel();
	}
	if (ResourceCount == 0)
	{
		PreviewResourceTickerHandle.Reset();
		LastReportedPreviewResourceCount = INDEX_NONE;
		return false;
	}
	return true;
}

void SFrameEventEditor::EnsurePreviewResourceTicker()
{
	if (!bHostActive || bIsPlaying || !PreviewHost || PreviewResourceTickerHandle.IsValid())
	{
		return;
	}
	const int32 ResourceCount = PreviewHost->GetOwnedResourceCount();
	if (ResourceCount == 0)
	{
		return;
	}
	LastReportedPreviewResourceCount = ResourceCount;
	PreviewResourceTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateSP(this, &SFrameEventEditor::OnPreviewResourceTick), 0.1f);
}

void SFrameEventEditor::StopPreviewResourceTicker()
{
	if (PreviewResourceTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(PreviewResourceTickerHandle);
		PreviewResourceTickerHandle.Reset();
	}
	LastReportedPreviewResourceCount = INDEX_NONE;
}

int32 SFrameEventEditor::GetFrameFromTime() const
{
	if (CachedTiming.TotalDurationSeconds <= 0.0f) return 0;
	return FrameEventEditorInternal::TimeToFrameIndex(CachedTiming, PlaybackTime);
}

void SFrameEventEditor::PreviewSelectedCue()
{
	if (!bHostActive || !PreviewHost || bIsPlaying || bDispatchingPreview)
	{
		return;
	}

	StopPreviewResourceTicker();
	// A selection preview of a Cue State is a real Begin session. End any prior inspection/scrub
	// session while its world still exists, then start from a clean, isolated subject.
	ForceEndAllActiveRangedEvents(EPaper2DPlusFrameCueEndReason::EditorReset);
	PreviewEvaluationMode =
		EPaper2DPlusFrameCueEvaluationMode::EditorSelectionPreview;
	UPaper2DPlusCueBase* Cue = ResolveCueIdentity(SelectedCueIdentity);
	if (!IsValid(Cue))
	{
		ReconcileSelectedCueIdentity();
		if (PreviewCanvasWidget.IsValid()) PreviewCanvasWidget->Invalidate();
		InvalidatePreviewPanel();
		return;
	}

	// Selection is an editor inspection gesture, but it executes the same Cue behavior as playback.
	// Cue States join the active set so Clear, seek, mutation, compile, tool switch, and tab teardown
	// all deliver their paired End before the preview world is cleaned.
	const int32 AnchorFrame = FMath::Max(0, Cue->GetPrimaryAnchorFrame());
	PreviewDisplayFrameIndex = AnchorFrame;
	FPaper2DPlusFrameCueContext Context =
		MakePreviewContext(
			AnchorFrame,
			AnchorFrame,
			false,
			PreviewEvaluationMode);
	Context.Phase = Cue->IsRangeCue()
		? EPaper2DPlusFrameCuePhase::Begin
		: EPaper2DPlusFrameCuePhase::Trigger;
#if WITH_EDITORONLY_DATA
	if (!Cue->bSkipInEditorPreview)
#endif
	{
		bDispatchingPreview = true;
		PreviewDispatchFrame = AnchorFrame;
		PreviewHost->BeginDispatchScope();
		ON_SCOPE_EXIT
		{
			FinishPreviewDispatchScope();
			PreviewDispatchFrame = INDEX_NONE;
		};
		if (Cue->IsRangeCue())
		{
			EditorActiveRangeCues.Add(TWeakObjectPtr<UPaper2DPlusCueBase>(Cue));
			// The paired End reports the frame where this inspection Begin happened, not a stale scrub
			// frame that happened to precede selection.
			LastPreviewFrame = AnchorFrame;
		}
		Paper2DPlusFrameCuePreviewBehavior::NotifyPreview(*Cue, Context, PreviewHost.Get());
	}
	// Creator adapters are extensible C++/Blueprint objects. Reconcile after their callback before any
	// caller is allowed to use the selected view index again.
	ReconcileSelectedCueIdentity();
	EnsurePreviewResourceTicker();
	if (PreviewCanvasWidget.IsValid()) PreviewCanvasWidget->Invalidate();
	InvalidatePreviewPanel();
}

void SFrameEventEditor::DispatchPreviewTransition(int32 NewFrame, bool bFromPlayback)
{
	if (!bHostActive || NewFrame == INDEX_NONE) return;

	// Re-entry guard (mirrors the runtime's bDispatchingFrameEvents): a preview BP handler that
	// synchronously re-enters this path must not mutate EditorActiveRangedEvents / LastPreviewFrame
	// out from under the outer call. Iteration is already snapshot-protected; this protects the set.
	if (bDispatchingPreview) return;

	const EPaper2DPlusFrameCueEvaluationMode DestinationMode =
		bFromPlayback
			? EPaper2DPlusFrameCueEvaluationMode::EditorPlayback
			: EPaper2DPlusFrameCueEvaluationMode::EditorScrubSeek;
	if (PreviewEvaluationMode != DestinationMode && CountLiveActiveRangeCues() > 0)
	{
		// Evaluation sessions never blend. Pair the outgoing range with its old mode before the first
		// callback from the destination playback/scrub session.
		ForceEndAllActiveRangedEvents(EPaper2DPlusFrameCueEndReason::EditorReset);
		LastPreviewFrame = INDEX_NONE;
	}
	PreviewEvaluationMode = DestinationMode;

	// A MANUAL backward scrub (NewFrame < LastPreviewFrame while NOT driven by playback) is a fresh
	// SEEK, not a playback loop wrap. Treat it like a flipbook reseek: force-end any active ranged
	// events (so a range we were inside doesn't leak its Begin), then reset to an unseeded state so the
	// seed-on-first-dispatch path below reseeds Prev=NewFrame. Without this, the old code marked every
	// backward selection as a loop wrap, and the one-shot/ranged loop-wrap rule (fire if frame >
	// PreviousFrame OR <= CurrentFrame) fired every start-of-animation event just because the author
	// scrubbed backward.
	if (!bFromPlayback && LastPreviewFrame != INDEX_NONE && NewFrame < LastPreviewFrame)
	{
		// End/reset is a complete host transaction. Any compile or registry edit requested by Cue End
		// stabilizes before the destination frame starts its independent dispatch below.
		ForceEndAllActiveRangedEvents(EPaper2DPlusFrameCueEndReason::EditorReset);
		// Drop to unseeded so the seed below makes the target dispatch a no-op span (target-only).
		LastPreviewFrame = INDEX_NONE;
	}

	TArray<TObjectPtr<UPaper2DPlusCueBase>>* Cues = GetFrameCuesArray();
	if (!Cues)
	{
		LastPreviewFrame = NewFrame;
		return;
	}

	// Seed-on-first-dispatch (Finding 1): when starting from an UNSEEDED state, the previous frame is
	// the CURRENT target frame, producing a no-op span N->N. Per the dispatch rules a same-frame span
	// fires ONLY events anchored on N (ranged events whose range contains N Begin; one-shots use their
	// same-frame rule) — it does NOT replay every one-shot on frames 0..N the way a -1..N span would.
	const int32 EffectivePrevFrame = LastPreviewFrame;

	// Loop/back detection: only a genuine PLAYBACK wrap (advancing past the last frame back toward 0
	// while playing) is a loop wrap. Manual backward scrubs were converted to a reseek above, so by
	// here a backward step can only be a playback wrap.
	const bool bLoopWrap = bFromPlayback && (EffectivePrevFrame != INDEX_NONE && NewFrame < EffectivePrevFrame);

	// Preview context carries the isolated subject actor/component. Scoped Cue GetWorld resolution makes
	// ordinary Blueprint spawn/trace/effect/sound calls behave like animation notifies without touching
	// the open editor level; optional registered adapters may still add specialized overlays afterward.
	const FPaper2DPlusFrameCueContext Context =
		MakePreviewContext(
			NewFrame,
			EffectivePrevFrame,
			bLoopWrap,
			PreviewEvaluationMode);

	// Snapshot so a handler can't invalidate iteration.
	TArray<TObjectPtr<UPaper2DPlusCueBase>> Snapshot = *Cues;

	{
		bDispatchingPreview = true;
		PreviewDispatchFrame = NewFrame;
		if (PreviewHost)
		{
			PreviewHost->BeginDispatchScope();
		}
		ON_SCOPE_EXIT
		{
			FinishPreviewDispatchScope();
			PreviewDispatchFrame = INDEX_NONE;
		};

		// Only live placements cross into shared dispatch; the ledger is rebuilt from what comes back.
		TSet<TObjectPtr<UPaper2DPlusCueBase>> LiveActiveRanges = ResolveActiveRangeCues();
		Paper2DPlusFrameCues::DispatchFrameTransition(
			Snapshot, Context, LiveActiveRanges,
			[](UPaper2DPlusCueBase& Cue)
			{
#if WITH_EDITORONLY_DATA
				// One gate for both sinks: a placement excluded from preview runs neither its behavior nor
				// its adapters, while its game dispatch is untouched.
				return !Cue.bSkipInEditorPreview;
#else
				return true;
#endif
			},
			[this](UPaper2DPlusCueBase& Cue, const FPaper2DPlusFrameCueContext& CueContext)
			{
				Paper2DPlusFrameCuePreviewBehavior::NotifyPreview(
					Cue, CueContext, PreviewHost.Get());
			});
		StoreActiveRangeCues(LiveActiveRanges);

		LastPreviewFrame = NewFrame;
	}
	EnsurePreviewResourceTicker();
	InvalidatePreviewPanel();
}

void SFrameEventEditor::ForceEndAllActiveRangedEvents(
	const EPaper2DPlusFrameCueEndReason EndReason,
	const bool bResetPreviewHost)
{
	StopPreviewResourceTicker();
	if (bDispatchingPreview)
	{
		// The active set may still live in DispatchFrameTransition's local snapshot. Queue the first
		// terminal request and drain it only after that snapshot has been committed back to the weak
		// ledger. Later requests may strengthen cleanup but cannot relabel the outgoing End.
		if (!bPreviewForceEndPending)
		{
			bPreviewForceEndPending = true;
			PendingPreviewEndReason = EndReason;
			PendingPreviewEndEvaluationMode = PreviewEvaluationMode;
			PendingPreviewEndFrame =
				PreviewDispatchFrame != INDEX_NONE
					? PreviewDispatchFrame
					: LastPreviewFrame;
		}
		bPendingPreviewHostReset |= bResetPreviewHost;
		return;
	}

	ExecuteForceEndAllActiveRangedEvents(
		EndReason,
		bResetPreviewHost,
		PreviewEvaluationMode,
		LastPreviewFrame);
}

void SFrameEventEditor::ExecuteForceEndAllActiveRangedEvents(
	const EPaper2DPlusFrameCueEndReason EndReason,
	const bool bResetPreviewHost,
	const EPaper2DPlusFrameCueEvaluationMode EvaluationMode,
	const int32 EndFrame)
{
	if (CountLiveActiveRangeCues() == 0)
	{
		EditorActiveRangeCues.Reset();
		if (PreviewHost && bResetPreviewHost) PreviewHost->Reset();
		InvalidatePreviewPanel();
		return;
	}

	bDispatchingPreview = true;
	if (PreviewHost)
	{
		PreviewHost->BeginDispatchScope();
	}
	ON_SCOPE_EXIT
	{
		FinishPreviewDispatchScope();
	};

	TArray<TObjectPtr<UPaper2DPlusCueBase>> AuthoredOrder;
	if (TArray<TObjectPtr<UPaper2DPlusCueBase>>* Cues = GetFrameCuesArray())
	{
		AuthoredOrder = *Cues;
	}
	FPaper2DPlusFrameCueContext EndContext =
		FPaper2DPlusFrameCueContext::MakePreview(
			INDEX_NONE,
			EndFrame,
			/*bLoopWrap=*/false,
			EvaluationMode);
	if (PreviewHost)
	{
		// End belongs to the source session, even when the model has already published a destination
		// Profile/animation swap. Keep the host's last synchronized identity until every outgoing Cue
		// State is paired; the next canvas tick synchronizes the destination after reset.
		PreviewHost->PopulateContext(EndContext);
	}
	TSet<TObjectPtr<UPaper2DPlusCueBase>> LiveActiveRanges = ResolveActiveRangeCues();
	Paper2DPlusFrameCues::ForceEndActiveRanges(
		AuthoredOrder,
		EndContext,
		EndReason,
		LiveActiveRanges,
		[this](UPaper2DPlusCueBase& Cue, const FPaper2DPlusFrameCueContext& CueContext)
		{
			Paper2DPlusFrameCuePreviewBehavior::NotifyPreview(Cue, CueContext, PreviewHost.Get());
		});
	StoreActiveRangeCues(LiveActiveRanges);
	LastPreviewEndReason = EndReason;
	LastPreviewEndEvaluationMode = EvaluationMode;
	LastPreviewEndFrame = EndFrame;
	if (PreviewHost && bResetPreviewHost) PreviewHost->Reset();
	InvalidatePreviewPanel();
}

void SFrameEventEditor::FinishPreviewDispatchScope()
{
	bDispatchingPreview = false;

	// Drain an End requested by Cue behavior/listeners before the outer host scope is allowed to
	// replace its world. The pending transaction captured the outgoing mode/frame at request time.
	if (bPreviewForceEndPending)
	{
		const EPaper2DPlusFrameCueEndReason EndReason = PendingPreviewEndReason;
		const EPaper2DPlusFrameCueEvaluationMode EvaluationMode =
			PendingPreviewEndEvaluationMode;
		const int32 EndFrame = PendingPreviewEndFrame;
		const bool bResetPreviewHost = bPendingPreviewHostReset;
		bPreviewForceEndPending = false;
		bPendingPreviewHostReset = false;
		PendingPreviewEndReason = EPaper2DPlusFrameCueEndReason::None;
		PendingPreviewEndFrame = INDEX_NONE;
		ExecuteForceEndAllActiveRangedEvents(
			EndReason,
			bResetPreviewHost,
			EvaluationMode,
			EndFrame);
		// The outer transition writes its destination frame before this scope exit runs. A terminal
		// request raised from inside that callback must still leave the session unseeded, just like
		// the same reset requested outside dispatch.
		LastPreviewFrame = INDEX_NONE;
	}

	if (PreviewHost)
	{
		PreviewHost->EndDispatchScope();
	}
}

// ─── FRAME SELECTION ─────────────────────────────────────────────────────────

void SFrameEventEditor::OnFrameClicked(int32 FrameIndex)
{
	if (!bHostActive || GetFrameCount() <= 0)
	{
		return;
	}
	// Always stop playback on a frame click — including clicking the cell the playhead is currently on
	// (the equality check comes AFTER the stop so the live-playhead frame still halts).
	StopPreviewResourceTicker();
	StopPlaybackTicker();
	const int32 ClampedFrame = FMath::Clamp(FrameIndex, 0, GetFrameCount() - 1);
	if (ClampedFrame != SelectedFrameIndex)
	{
		// The shared model owns the one frame selection. Its handler performs the seek dispatch and repaints
		// every active/hidden mode without a second local clamp or echo loop.
		if (Model.IsValid()) Model->SetSelectedFrame(ClampedFrame);
		else
		{
			SelectedFrameIndex = ClampedFrame;
			PreviewDisplayFrameIndex = ClampedFrame;
			DispatchPreviewTransition(ClampedFrame);
		}
	}
	else
	{
		PreviewSelectedCue();
	}
	if (FrameStripBox.IsValid()) FrameStripBox->Invalidate(EInvalidateWidgetReason::Paint);
	if (TimelineTrack.IsValid()) TimelineTrack->Invalidate(EInvalidateWidgetReason::Paint);
	if (CurveStack.IsValid()) CurveStack->InvalidateScrub();
	Invalidate(EInvalidateWidgetReason::Paint);
}

// ─── EVENT MANAGEMENT ────────────────────────────────────────────────────────

void SFrameEventEditor::CollectFrameCueClasses(TArray<UClass*>& OutMomentClasses, TArray<UClass*>& OutRangeClasses)
{
	OutMomentClasses.Reset();
	OutRangeClasses.Reset();

	const FPaper2DPlusFrameCueTypeDiscoveryResult Discovery =
		FPaper2DPlusFrameCueTypeAuthoring::DiscoverCueTypes();
	for (const FPaper2DPlusFrameCueTypeDescriptor& Type : Discovery.Types)
	{
		UClass* CueClass = Type.Class.Get();
		if (!CueClass)
		{
			continue;
		}
		// Automation fixtures stay Ready for the placement gate but never reach a designer's
		// list (TASK-173).
		if (FPaper2DPlusFrameCueTypeAuthoring::IsAutomationFixtureCueClass(CueClass))
		{
			continue;
		}
		if (Type.Kind == EPaper2DPlusFrameCueTypeKind::Range)
		{
			OutRangeClasses.Add(CueClass);
		}
		else if (Type.Kind == EPaper2DPlusFrameCueTypeKind::Moment)
		{
			OutMomentClasses.Add(CueClass);
		}
	}
}

void SFrameEventEditor::ShowAddEventPicker()
{
	if (!CanMutateLiveSelection())
	{
		return;
	}
	if (!DataProvider.IsValid())
	{
		return;
	}
	const FProfileScopedAnimationIdentity TargetScope =
		DataProvider->GetScopedAnimationIdentity(SelectedFlipbookIndex);
	const FGuid TargetTrackId = UnifiedTimeline.IsValid()
		? UnifiedTimeline->GetActiveTrackId()
		: FGuid();
	const FPaper2DPlusFrameCuePlacementTarget Target =
		Paper2DPlusFrameCuePlacementAuthoring::CaptureTarget(
			DataProvider, TargetScope, SelectedFrameIndex, TargetTrackId);
	ShowAddEventPicker(Target);
}

void SFrameEventEditor::QueueAddEventPicker(
	const int32 TargetFrameIndex,
	const FGuid TargetTrackId)
{
	if (!CanMutateLiveSelection() || !DataProvider.IsValid())
	{
		return;
	}
	const FProfileScopedAnimationIdentity TargetScope =
		DataProvider->GetScopedAnimationIdentity(SelectedFlipbookIndex);
	const FPaper2DPlusFrameCuePlacementTarget Target =
		Paper2DPlusFrameCuePlacementAuthoring::CaptureTarget(
			DataProvider, TargetScope, TargetFrameIndex, TargetTrackId);
	if (!Target.IsValid())
	{
		return;
	}

	const TWeakPtr<SFrameEventEditor> WeakEditor = SharedThis(this);
	RegisterActiveTimer(
		0.0f,
		FWidgetActiveTimerDelegate::CreateLambda(
			[WeakEditor, Target](double, float)
			{
				if (const TSharedPtr<SFrameEventEditor> Editor = WeakEditor.Pin())
				{
					if (Editor->UnifiedTimeline.IsValid())
					{
						Editor->UnifiedTimeline->SetActiveTrackId(Target.CapturedTrackId);
					}
					Editor->OnFrameClicked(Target.CapturedFrame);
					Editor->ShowAddEventPicker(Target);
				}
				return EActiveTimerReturnType::Stop;
			}));
}

void SFrameEventEditor::ShowAddEventPicker(
	const FPaper2DPlusFrameCuePlacementTarget& Target)
{
	if (!CanMutateLiveSelection() || !Target.IsValid())
	{
		return;
	}

	const TWeakPtr<SFrameEventEditor> WeakEditor = SharedThis(this);
	const TSharedRef<SWidget> Picker =
		SNew(SPaper2DPlusFrameCueTypePicker)
		.FocusReturnTarget(UnifiedTimeline.IsValid()
			? UnifiedTimeline->GetAddCueFocusTarget()
			: TWeakPtr<SWidget>())
		.OnPickType(TFunction<void(UClass*)>(
			[WeakEditor, Target](UClass* CueClass)
			{
				if (const TSharedPtr<SFrameEventEditor> PinnedEditor = WeakEditor.Pin())
				{
					PinnedEditor->AddFrameCue(CueClass, Target);
				}
			}))
		.OnCreateType(TFunction<void()>(
			[WeakEditor, Target]()
			{
				if (const TSharedPtr<SFrameEventEditor> PinnedEditor = WeakEditor.Pin())
				{
					PinnedEditor->CreateNewFrameCueType(Target);
				}
			}))
		// A disabled diagnostic row is only half the fix. Clicking it opens the offending type in the
		// restricted Cue Type editor, which is where every reason the row can carry is repaired.
		.OnRepairType(TFunction<void(const FPaper2DPlusFrameCueTypeDescriptor&)>(
			[WeakEditor](const FPaper2DPlusFrameCueTypeDescriptor& RejectedType)
			{
				const TSharedPtr<SFrameEventEditor> PinnedEditor = WeakEditor.Pin();
				if (!PinnedEditor.IsValid())
				{
					return;
				}
				if (!Paper2DPlusFrameCueTypePicker::OpenForRepair(RejectedType))
				{
					FrameEventEditorInternal::ShowFailureNotification(FText::Format(
						LOCTEXT(
							"CueTypeRepairUnavailable",
							"'{0}' cannot be opened for repair. Fix or delete the asset in the Content Browser."),
						RejectedType.PickerLabel.IsEmpty()
							? RejectedType.DisplayName
							: RejectedType.PickerLabel));
				}
			}));

	FSlateApplication::Get().PushMenu(
		SharedThis(this),
		FWidgetPath(),
		Picker,
		FSlateApplication::Get().GetCursorPos(),
		FPopupTransitionEffect::ContextMenu);
}

void SFrameEventEditor::CreateNewFrameCueType(
	const FPaper2DPlusFrameCuePlacementTarget& Target)
{
	if (!CanMutateLiveSelection() || !Target.IsValid())
	{
		return;
	}
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	const FString DefaultPath = TEXT("/Game/Blueprints/FrameCues");
	FString UniquePackageName;
	FString UniqueAssetName;
	AssetTools.CreateUniqueAssetName(
		DefaultPath / TEXT("NewFrameCueType"),
		TEXT(""),
		UniquePackageName,
		UniqueAssetName);

	UPaper2DPlusFrameCueTypeFactory* Factory =
		NewObject<UPaper2DPlusFrameCueTypeFactory>();
	Factory->bEditAfterNew = false;
	UObject* NewAsset = AssetTools.CreateAssetWithDialog(
		UniqueAssetName,
		FPackageName::GetLongPackagePath(UniquePackageName),
		UPaper2DPlusFrameCueBlueprint::StaticClass(),
		Factory);
	UPaper2DPlusFrameCueBlueprint* CueType =
		Cast<UPaper2DPlusFrameCueBlueprint>(NewAsset);
	if (!CueType)
	{
		if (!Factory->GetLastCreateError().IsEmpty())
		{
			FrameEventEditorInternal::ShowFailureNotification(
				Factory->GetLastCreateError());
		}
		return;
	}

	const EPaper2DPlusFrameCueTypeKind Kind =
		FPaper2DPlusFrameCueTypeAuthoring::ClassifyKind(CueType->GeneratedClass);
	if (Kind == EPaper2DPlusFrameCueTypeKind::Invalid)
	{
		FrameEventEditorInternal::ShowFailureNotification(LOCTEXT(
			"CreatedCueTypeInvalidKind",
			"The new Cue Type was created, but its Cue or Cue State lifecycle could not be resolved."));
		return;
	}
	const EPaper2DPlusFrameCuePlacementKind PlacementKind =
		Kind == EPaper2DPlusFrameCueTypeKind::Range
			? EPaper2DPlusFrameCuePlacementKind::Range
			: EPaper2DPlusFrameCuePlacementKind::Moment;
	const TSharedRef<FPaper2DPlusFrameCuePendingPlacement> Pending =
		CreatePendingPlacement(
			Target, PlacementKind, /*bReadyToPlace*/ false);
	const TSharedRef<FPaper2DPlusFrameCueTypeEditor> CueTypeEditor =
		MakeShared<FPaper2DPlusFrameCueTypeEditor>();
	CueTypeEditor->InitFrameCueTypeEditor(
		EToolkitMode::Standalone,
		TSharedPtr<IToolkitHost>(),
		CueType,
		Pending);

	// The dialog named this asset up front, so its first protected save can happen right here and
	// nothing is left to rename afterwards. It runs AFTER the toolkit opens on purpose: opening a
	// Blueprint editor can still touch the asset, and a save the open then dirties again would put
	// the brand-new type straight back into the "needs save" state this exists to remove.
	const FPaper2DPlusFrameCueTypeBirthReadyResult BirthReady =
		FPaper2DPlusFrameCueTypeBirthReady::CompleteNow(*CueType);
	if (!BirthReady.IsSuccess())
	{
		FPaper2DPlusFrameCueTypeBirthReady::ReportFailure(BirthReady, CueType);
	}
}

TSharedRef<FPaper2DPlusFrameCuePendingPlacement> SFrameEventEditor::CreatePendingPlacement(
	const FPaper2DPlusFrameCuePlacementTarget& Target,
	EPaper2DPlusFrameCuePlacementKind Kind,
	bool bReadyToPlace)
{
	const TWeakPtr<SFrameEventEditor> WeakOrigin = SharedThis(this);
	return FPaper2DPlusFrameCuePendingPlacement::Create(
		Target,
		Kind,
		bReadyToPlace,
		[WeakOrigin, Target](const FPaper2DPlusFrameCuePlacementResult& Result)
		{
			if (const TSharedPtr<SFrameEventEditor> Origin = WeakOrigin.Pin())
			{
				Origin->HandlePendingPlacementCompleted(Target, Result);
			}
		});
}

void SFrameEventEditor::HandlePendingPlacementCompleted(
	const FPaper2DPlusFrameCuePlacementTarget& Target,
	const FPaper2DPlusFrameCuePlacementResult& Result)
{
	if (Result.Status != EPaper2DPlusFrameCuePlacementStatus::Success)
	{
		return;
	}
	++PendingPlacementCompletionCountForTests;

	if (!Model.IsValid() || !DataProvider.IsValid() || !Target.IsValid())
	{
		return;
	}

	// AnimationIndex resolves against the Profile only (layer-independent), so it is valid to compute it
	// BEFORE deciding whether to restore Layer navigation.
	const int32 AnimationIndex = DataProvider->ResolveAnimationIndex(Target.Scope);
	if (AnimationIndex == INDEX_NONE)
	{
		// The snapshot may have validly appended to its strongly retained source after this editor switched
		// to another Profile. In that case do not retarget the live origin to an unrelated asset.
		return;
	}

	// Restore the exact captured source scope before resolving the placed Cue's identity. A Layer provider
	// rejects identities from any other selected LayerId (FFrameCueDataProvider::ResolveCueIndex scope gate),
	// so this ordering is load-bearing for Profile/Layer parity WHEN we do resolve the identity. But an async
	// save can complete long after the designer navigated to another Layer/animation: yanking selection back
	// there mid-edit is the reported regression. Only restore navigation when the designer is STILL on the
	// captured Layer; otherwise the placement already succeeded on the captured Layer's data â€” confirm it with
	// a non-stealing toast and leave selection untouched.
	if (Target.Scope.LayerScope.bLayerScoped)
	{
		if (!Target.Scope.LayerScope.LayerId.IsValid())
		{
			return;
		}
		if (Model->GetSelectedLayerId() != Target.Scope.LayerScope.LayerId)
		{
			FString LayerDisplayName = Target.Scope.LayerScope.LayerId.ToString(EGuidFormats::DigitsWithHyphens);
			if (const UPaper2DPlusCharacterLayerAsset* CapturedLayerAsset =
					Cast<UPaper2DPlusCharacterLayerAsset>(Model->GetSecondaryWatchedObject()))
			{
				if (const FCharacterLayer* CapturedLayer =
						CapturedLayerAsset->GetLayerById(Target.Scope.LayerScope.LayerId);
					CapturedLayer && !CapturedLayer->LayerName.IsEmpty())
				{
					LayerDisplayName = CapturedLayer->LayerName;
				}
			}
			FString AnimationDisplayName = Target.Scope.Animation.FallbackName;
			if (const UPaper2DPlusCharacterProfileAsset* Profile = Model->GetAsset();
				Profile && Profile->Flipbooks.IsValidIndex(AnimationIndex))
			{
				AnimationDisplayName = Profile->Flipbooks[AnimationIndex].Identity.FlipbookName;
			}
			FrameEventEditorInternal::ShowNonStealingNotification(FText::Format(
				LOCTEXT(
					"CuePlacedOnOtherLayer",
					"Cue placed on {0} - {1} frame {2}"),
				FText::FromString(LayerDisplayName),
				FText::FromString(AnimationDisplayName),
				FText::AsNumber(Target.CapturedFrame)));
			return;
		}
		// Same-layer: no-op restore keeps the model on the captured Layer for identity resolution below.
		Model->SetSelectedLayerById(Target.Scope.LayerScope.LayerId);
	}

	Model->SetSelectedFlipbook(AnimationIndex);
	Model->SetSelectedFrame(Target.CapturedFrame);
	SelectedFlipbookIndex = AnimationIndex;
	SelectedFrameIndex = Target.CapturedFrame;
	SelectedCueIdentity = Result.PlacementIdentity;
	ReconcileSelectedCueIdentity();
	if (bHostActive)
	{
		RefreshAll();
		PreviewSelectedCue();
	}
	else
	{
		bNeedsRefresh = true;
	}
}

TSharedRef<FPaper2DPlusFrameCuePendingPlacement>
SFrameEventEditor::CreatePendingPlacementForTests(
	const FPaper2DPlusFrameCuePlacementTarget& Target,
	EPaper2DPlusFrameCuePlacementKind Kind,
	bool bReadyToPlace)
{
	return CreatePendingPlacement(Target, Kind, bReadyToPlace);
}

bool SFrameEventEditor::CanEditSelectedCueType() const
{
	const UPaper2DPlusCueBase* Cue = ResolveCueIdentity(SelectedCueIdentity);
	UBlueprint* CueTypeBlueprint = nullptr;
	if (!IsValid(Cue))
	{
		return false;
	}
	const Paper2DPlusFrameCueEditorAuthoring::EPaper2DPlusFrameCueTypeEditRoute Route =
		Paper2DPlusFrameCueEditorAuthoring::ResolveCueTypeEditRoute(
			Cue->GetClass(), CueTypeBlueprint);
	return Route != Paper2DPlusFrameCueEditorAuthoring::
		EPaper2DPlusFrameCueTypeEditRoute::None
		&& FPaper2DPlusFrameCueTypeEditor::IsSupportedBlueprint(CueTypeBlueprint);
}

void SFrameEventEditor::EditSelectedCueType()
{
	UPaper2DPlusCueBase* Cue = ResolveCueIdentity(SelectedCueIdentity);
	UBlueprint* CueTypeBlueprint = nullptr;
	if (!IsValid(Cue))
	{
		return;
	}
	const Paper2DPlusFrameCueEditorAuthoring::EPaper2DPlusFrameCueTypeEditRoute Route =
		Paper2DPlusFrameCueEditorAuthoring::ResolveCueTypeEditRoute(
			Cue->GetClass(), CueTypeBlueprint);
	if (Route == Paper2DPlusFrameCueEditorAuthoring::
			EPaper2DPlusFrameCueTypeEditRoute::None
		|| !FPaper2DPlusFrameCueTypeEditor::IsSupportedBlueprint(CueTypeBlueprint))
	{
		return;
	}
	if (Route == Paper2DPlusFrameCueEditorAuthoring::
		EPaper2DPlusFrameCueTypeEditRoute::LegacyRecoveryRequired)
	{
		// Generic legacy Cue Blueprints must bypass their ordinary asset action or Unreal would
		// immediately expose the unrestricted graph editor. The restricted surface inventories
		// preserved behavior and offers the warning-gated standard-editor escape hatch explicitly.
		const TSharedRef<FPaper2DPlusFrameCueTypeEditor> CueTypeEditor =
			MakeShared<FPaper2DPlusFrameCueTypeEditor>();
		CueTypeEditor->InitFrameCueTypeEditor(
			EToolkitMode::Standalone,
			TSharedPtr<IToolkitHost>(),
			CueTypeBlueprint,
			nullptr);
		return;
	}
	if (!GEditor)
	{
		return;
	}

	// Specialized asset actions own the double-click contract and instantiate only the restricted,
	// behavior-capable Cue Type editor. Routing through the subsystem also reuses an already-open toolkit.
	if (UAssetEditorSubsystem* AssetEditors =
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
	{
		AssetEditors->OpenEditorForAsset(CueTypeBlueprint);
	}
}

void SFrameEventEditor::ResetPreviewForCueMutation(
	UPaper2DPlusCueBase* Cue,
	EPaper2DPlusFrameCueEndReason EndReason)
{
	Paper2DPlusFrameCueEditorAuthoring::EndActiveRangePreview(
		Cue,
		EndReason,
		LastPreviewFrame,
		EditorActiveRangeCues,
		PreviewHost.Get(),
		nullptr,
		/*bResetPreviewHost*/ false,
		PreviewEvaluationMode);

	// Adapter resources are owner-scoped, not cue-scoped. A global host reset therefore invalidates
	// every overlapping range, so pair all surviving Begins before the single reset boundary.
	if (CountLiveActiveRangeCues() > 0)
	{
		ForceEndAllActiveRangedEvents(EndReason);
	}
	else if (PreviewHost)
	{
		StopPreviewResourceTicker();
		PreviewHost->Reset();
	}
	InvalidatePreviewPanel();
}

void SFrameEventEditor::AddFrameCue(
	UClass* CueClass,
	const FProfileScopedAnimationIdentity& TargetScope)
{
	const FPaper2DPlusFrameCuePlacementTarget Target =
		Paper2DPlusFrameCuePlacementAuthoring::CaptureTarget(
			DataProvider,
			TargetScope,
			SelectedFrameIndex,
			UnifiedTimeline.IsValid() ? UnifiedTimeline->GetActiveTrackId() : FGuid());
	AddFrameCue(CueClass, Target);
}

void SFrameEventEditor::AddFrameCue(
	UClass* CueClass,
	const FPaper2DPlusFrameCuePlacementTarget& Target)
{
	if (!CanMutateLiveSelection() || HasActiveTransaction() || !Target.IsValid())
	{
		return;
	}
	const EPaper2DPlusFrameCueTypeKind CueKind =
		FPaper2DPlusFrameCueTypeAuthoring::ClassifyKind(CueClass);
	if (CueKind == EPaper2DPlusFrameCueTypeKind::Invalid)
	{
		return;
	}
	const EPaper2DPlusFrameCuePlacementKind PlacementKind =
		CueKind == EPaper2DPlusFrameCueTypeKind::Range
			? EPaper2DPlusFrameCuePlacementKind::Range
			: EPaper2DPlusFrameCuePlacementKind::Moment;
	const TSharedRef<FPaper2DPlusFrameCuePendingPlacement> Pending =
		FPaper2DPlusFrameCuePendingPlacement::Create(Target, PlacementKind, true);
	const FPaper2DPlusFrameCuePlacementResult Placement = Pending->Commit(CueClass);
	if (Placement.Status != EPaper2DPlusFrameCuePlacementStatus::Success)
	{
		FrameEventEditorInternal::ShowFailureNotification(LOCTEXT(
			"AddCueAtomicFailure",
			"The Cue could not be added to the captured animation frame; no placement was changed."));
		return;
	}
	SelectedCueIdentity = Placement.PlacementIdentity;

	ReconcileSelectedCueIdentity();
	if (UnifiedTimeline.IsValid()) UnifiedTimeline->HandleCueSelected(SelectedEventIndex);
	PreviewSelectedCue();
	RefreshDetailsPanel();
	if (FrameStripBox.IsValid()) FrameStripBox->Invalidate(EInvalidateWidgetReason::Paint);
	if (TimelineTrack.IsValid()) TimelineTrack->Invalidate(EInvalidateWidgetReason::Layout);
}

void SFrameEventEditor::DuplicateEvent(int32 EventIndex)
{
	if (!CanMutateLiveSelection() || HasActiveTransaction()) return;
	const TArray<TObjectPtr<UPaper2DPlusCueBase>>* Cues = GetFrameCues();
	UPaper2DPlusCueBase* SourceCue =
		Cues && Cues->IsValidIndex(EventIndex) ? (*Cues)[EventIndex].Get() : nullptr;
	if (!DataProvider.IsValid() || !IsValid(SourceCue))
	{
		return;
	}
	const FFrameCueStableIdentity SourceIdentity =
		DataProvider->GetCueIdentity(SelectedFlipbookIndex, SourceCue);
	if (!SourceIdentity.IsValid()) return;

	SelectedCueIdentity = DataProvider->DuplicateCue(SourceIdentity);
	if (!SelectedCueIdentity.IsValid()) return;

	ReconcileSelectedCueIdentity();
	if (UnifiedTimeline.IsValid()) UnifiedTimeline->HandleCueSelected(SelectedEventIndex);
	PreviewSelectedCue();
	RefreshDetailsPanel();
	if (FrameStripBox.IsValid()) FrameStripBox->Invalidate(EInvalidateWidgetReason::Paint);
	if (TimelineTrack.IsValid()) TimelineTrack->Invalidate(EInvalidateWidgetReason::Layout);
}

void SFrameEventEditor::RemoveEvent(int32 EventIndex)
{
	if (!CanMutateLiveSelection() || HasActiveTransaction()) return;
	const TArray<TObjectPtr<UPaper2DPlusCueBase>>* Cues = GetFrameCues();
	UPaper2DPlusCueBase* Removed =
		Cues && Cues->IsValidIndex(EventIndex) ? (*Cues)[EventIndex].Get() : nullptr;
	if (!DataProvider.IsValid() || !IsValid(Removed)) return;

	const FFrameCueStableIdentity RemovedIdentity =
		DataProvider->GetCueIdentity(SelectedFlipbookIndex, Removed);
	const bool bRemovedSelection = ResolveCueIdentity(SelectedCueIdentity) == Removed;
	// Pair a live Cue State before its owning reference disappears. This delivers exactly one
	// SourceRemoved End through the same preview host as normal lifecycle notifications, then resets
	// the host ledger so adapter resources cannot outlive the placement.
	ResetPreviewForCueMutation(Removed, EPaper2DPlusFrameCueEndReason::SourceRemoved);

	// Preview adapters are creator-extensible callbacks. They may have reordered, removed, or replaced
	// the source array, so reacquire it and resolve the placement identity before the destructive write.
	Removed = ResolveCueIdentity(RemovedIdentity);
	if (!Removed)
	{
		ReconcileSelectedCueIdentity();
		RefreshDetailsPanel();
		if (FrameStripBox.IsValid()) FrameStripBox->Invalidate(EInvalidateWidgetReason::Paint);
		if (TimelineTrack.IsValid()) TimelineTrack->Invalidate(EInvalidateWidgetReason::Layout);
		return;
	}

	if (!DataProvider->DeleteCue(RemovedIdentity))
	{
		ReconcileSelectedCueIdentity();
		RefreshDetailsPanel();
		return;
	}

	if (bRemovedSelection)
	{
		SelectedEventIndex = INDEX_NONE;
		SelectedCueIdentity = {};
		if (UnifiedTimeline.IsValid()) UnifiedTimeline->ClearPrimarySelection();
	}
	else
	{
		ReconcileSelectedCueIdentity();
	}
	RefreshDetailsPanel();
	if (FrameStripBox.IsValid()) FrameStripBox->Invalidate(EInvalidateWidgetReason::Paint);
	if (TimelineTrack.IsValid()) TimelineTrack->Invalidate(EInvalidateWidgetReason::Layout);
}

void SFrameEventEditor::OnEventSelected(int32 EventIndex)
{
	if (!bHostActive)
	{
		return;
	}
	SelectedEventIndex = EventIndex;
	if (const TArray<TObjectPtr<UPaper2DPlusCueBase>>* Cues = GetFrameCues();
		DataProvider.IsValid() && Cues && Cues->IsValidIndex(EventIndex))
	{
		SelectedCueIdentity = DataProvider->GetCueIdentity(
			SelectedFlipbookIndex, (*Cues)[EventIndex].Get());
	}
	else
	{
		SelectedCueIdentity = {};
	}
	WarmCurrentAnimationCueEffects();
	PreviewSelectedCue();
	ReconcileSelectedCueIdentity();
	if (UnifiedTimeline.IsValid()) UnifiedTimeline->HandleCueSelected(SelectedEventIndex);
	Invalidate(EInvalidateWidgetReason::Paint);
	if (TimelineTrack.IsValid()) TimelineTrack->Invalidate(EInvalidateWidgetReason::Paint);
	RefreshDetailsPanel();
}

void SFrameEventEditor::HandleTimelinePrimarySelectionChanged(
	const Paper2DPlusFrameCueTimeline::FPrimarySelection& Selection)
{
	if (!bHostActive)
	{
		return;
	}
	const FName TimelineCurve =
		Selection.Kind == Paper2DPlusFrameCueTimeline::EPrimarySelectionKind::Curve
			? Selection.CurveName
			: NAME_None;
	if (CurveStack.IsValid())
	{
		// The timeline owns the one primary selection. Clear the curve presentation selection when
		// a Cue/track/empty target wins, but never bounce that synchronization back into the timeline.
		CurveStack->SynchronizeSelection(TimelineCurve);
	}
	if (SelectedCurveRenameEditor.IsValid()
		&& SelectedCurveRenameEditor->HasKeyboardFocus()
		&& !CurveRenameSubject.IsNone()
		&& CurveRenameSubject != TimelineCurve)
	{
		// Mouse-down changes the timeline selection before Slate moves keyboard focus. Commit the text
		// against the curve it was authored for, not the newly selected subject.
		CommitSelectedCurveRename(
			SelectedCurveRenameEditor->GetText(),
			ETextCommit::OnUserMovedFocus);
		ResetCurveRenameEditorToSelection();
	}
	if (Selection.Kind == Paper2DPlusFrameCueTimeline::EPrimarySelectionKind::Cue)
	{
		return;
	}
	if (!bIsPlaying)
	{
		// Leaving Cue selection ends an inspection-only Cue State before its Details subject and
		// preview identity disappear. Playback owns its own lifecycle and remains continuous.
		ForceEndAllActiveRangedEvents();
		PreviewDisplayFrameIndex = SelectedFrameIndex;
	}
	SelectedEventIndex = INDEX_NONE;
	SelectedCueIdentity = {};
	RefreshDetailsPanel();
	if (TimelineTrack.IsValid())
	{
		TimelineTrack->Invalidate(EInvalidateWidgetReason::Paint);
	}
}

// ─── KEYBOARD ────────────────────────────────────────────────────────────────

FReply SFrameEventEditor::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (!bHostActive)
	{
		return FReply::Unhandled();
	}
	if (Paper2DPlusEditor::SlateShortcutUtils::ShouldIgnoreShortcutForFocusedWidget())
	{
		return FReply::Unhandled();
	}

	TSharedPtr<SWidget> Focused = FSlateApplication::Get().GetKeyboardFocusedWidget();
	if (Focused.IsValid())
	{
		FName Type = Focused->GetType();
		if (Type == TEXT("SCurveEditor"))
		{
			// F11 (ADV-9) — suppress-or-coerce house rule, COERCE chosen: a curve-track row grabs
			// keyboard focus on mouse-down (research risk 8), but SpaceBar/Left/Right (and the other
			// nav keys) are MEANINGLESS to SCurveEditor — playback and frame scrub must keep working
			// after a row click, so they fall THROUGH to the panel handling below. Refuse ONLY
			// Delete/Backspace (the double-delete: SCurveEditor binds Delete to delete-selected-keys,
			// and when it leaves the key unhandled the panel must not also delete the selected EVENT)
			// and modifier-chorded keys (SCurveEditor/global shortcuts own those).
			const FKey FocusedKey = InKeyEvent.GetKey();
			const bool bChorded = InKeyEvent.IsControlDown() || InKeyEvent.IsAltDown()
				|| InKeyEvent.IsShiftDown() || InKeyEvent.IsCommandDown();
			if (bChorded || FocusedKey == EKeys::Delete || FocusedKey == EKeys::BackSpace)
			{
				return FReply::Unhandled();
			}
		}
	}

	const FKey Key = InKeyEvent.GetKey();

	// Standard duplicate placement shortcut. Keep the selected source intact and select the new
	// placement so a designer can immediately edit its per-placement overrides.
	if (InKeyEvent.IsControlDown() && Key == EKeys::D && SelectedEventIndex != INDEX_NONE)
	{
		DuplicateEvent(SelectedEventIndex);
		return FReply::Handled();
	}

	// Guard remaining Ctrl chords (Ctrl+S save, etc.)
	if (InKeyEvent.IsControlDown())
	{
		return FReply::Unhandled();
	}
	if (InKeyEvent.IsAltDown() || InKeyEvent.IsCommandDown())
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

	// Delete/Backspace: remove selected event
	if (Key == EKeys::Delete || Key == EKeys::BackSpace)
	{
		if (SelectedEventIndex != INDEX_NONE)
		{
			RemoveEvent(SelectedEventIndex);
			return FReply::Handled();
		}
	}

	// Up/Down: queue-aware flipbook navigation
	if (Key == EKeys::Up && Model.IsValid())
	{
		StopPlaybackForBoundary(EPaper2DPlusFrameCueEndReason::AnimationChanged);
		if (Model->StepQueue(-1) == INDEX_NONE)
		{
			int32 NewIdx = Model->GetVisualAdjacentFlipbookIndex(-1);
			if (NewIdx != INDEX_NONE) Model->SetSelectedFlipbook(NewIdx);
		}
		return FReply::Handled();
	}
	if (Key == EKeys::Down && Model.IsValid())
	{
		StopPlaybackForBoundary(EPaper2DPlusFrameCueEndReason::AnimationChanged);
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
			// StepQueue lands the playhead on the previous animation's LAST frame. Without a queue
			// entry to step to, wrap inside this flipbook — the old code swallowed the key here, so
			// the playhead stuck on frame 0 (and symmetrically on the last frame going right).
			StopPlaybackForBoundary(EPaper2DPlusFrameCueEndReason::PlaybackStopped);
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
			StopPlaybackForBoundary(EPaper2DPlusFrameCueEndReason::PlaybackStopped);
			if (Model->StepQueue(1) == INDEX_NONE)
			{
				if (GetFrameCount() > 1) OnFrameClicked(0);
			}
		}
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

// ─── HELPERS ─────────────────────────────────────────────────────────────────

FFlipbookProfileEntry* SFrameEventEditor::GetSelectedFlipbookData() const
{
	UPaper2DPlusCharacterProfileAsset* AssetPtr = Model.IsValid() ? Model->GetAsset() : nullptr;
	if (!AssetPtr || !AssetPtr->Flipbooks.IsValidIndex(SelectedFlipbookIndex)) return nullptr;
	return &AssetPtr->Flipbooks[SelectedFlipbookIndex];
}

UPaperFlipbook* SFrameEventEditor::GetSelectedFlipbook() const
{
	FFlipbookProfileEntry* Data = GetSelectedFlipbookData();
	if (!Data || Data->Identity.Flipbook.IsNull()) return nullptr;
	return Data->Identity.Flipbook.Get();
}

UPaperFlipbook* SFrameEventEditor::GetPreviewFlipbook() const
{
	return Model.IsValid() && Model->IsDirectionalPreviewEnabled()
		? Model->GetDirectionalPreviewFlipbook()
		: GetSelectedFlipbook();
}

FPaper2DPlusFrameCueContext SFrameEventEditor::MakePreviewContext(
	const int32 CurrentFrame,
	const int32 PreviousFrame,
	const bool bWasLoopWrap,
	const EPaper2DPlusFrameCueEvaluationMode EvaluationMode) const
{
	FPaper2DPlusFrameCueContext Context =
		FPaper2DPlusFrameCueContext::MakePreview(
			CurrentFrame,
			PreviousFrame,
			bWasLoopWrap,
			EvaluationMode);
	if (PreviewHost)
	{
		const FFlipbookProfileEntry* Entry = GetSelectedFlipbookData();
		PreviewHost->PopulateContext(
			Context,
			Model.IsValid() ? Model->GetAsset() : nullptr,
			GetSelectedFlipbook(),
			Entry ? FName(*Entry->Identity.FlipbookName) : NAME_None);
	}
	return Context;
}

/**
 * The panel's ONLY synchronous art-load boundary, and the reason Slate paint stays resident-only.
 *
 * Every placement in the current animation is asked, through the generic
 * UPaper2DPlusCueBase::CollectWarmableEffectArt, which Paper Flipbooks it will need. No Cue class is
 * named here: every scalar soft flipbook field follows the same cooked structural rule. The loads
 * happen HERE, once per refresh, and the strong handles are held for the panel's lifetime so the
 * row/layout/paint paths that follow can read `.Get()` without ever touching the loader. Deleting
 * this reintroduces a first-paint hitch.
 */
void SFrameEventEditor::WarmCurrentAnimationCueEffects()
{
	WarmedAnimationCueEffectFlipbooks.Reset();
	const TArray<TObjectPtr<UPaper2DPlusCueBase>>* Cues = GetFrameCues();
	if (!Cues)
	{
		return;
	}

	TArray<TSoftObjectPtr<UPaperFlipbook>> WarmableArt;
	TSet<UPaperFlipbook*> SeenFlipbooks;
	for (const TObjectPtr<UPaper2DPlusCueBase>& Cue : *Cues)
	{
		const UPaper2DPlusCueBase* Placement = Cue.Get();
		if (!IsValid(Placement))
		{
			continue;
		}
		WarmableArt.Reset();
		Placement->CollectWarmableEffectArt(WarmableArt);
		for (const TSoftObjectPtr<UPaperFlipbook>& Reference : WarmableArt)
		{
			if (Reference.IsNull())
			{
				continue;
			}
			UPaperFlipbook* LoadedEffect = Reference.LoadSynchronous();
			if (LoadedEffect && !SeenFlipbooks.Contains(LoadedEffect))
			{
				SeenFlipbooks.Add(LoadedEffect);
				WarmedAnimationCueEffectFlipbooks.Emplace(LoadedEffect);
			}
		}
	}
}

int32 SFrameEventEditor::GetFrameCount() const
{
	return DataProvider.IsValid() ? DataProvider->GetFrameCount(SelectedFlipbookIndex) : 0;
}

const TArray<TObjectPtr<UPaper2DPlusCueBase>>* SFrameEventEditor::GetFrameCues() const
{
	return DataProvider.IsValid() ? DataProvider->GetCues(SelectedFlipbookIndex) : nullptr;
}

TArray<TObjectPtr<UPaper2DPlusCueBase>>* SFrameEventEditor::GetFrameCuesArray() const
{
	return DataProvider.IsValid() ? DataProvider->GetCuesMutable(SelectedFlipbookIndex) : nullptr;
}

UPaper2DPlusCueBase* SFrameEventEditor::ResolveCueIdentity(
	const FFrameCueStableIdentity& CueIdentity,
	int32* OutCurrentIndex) const
{
	if (OutCurrentIndex)
	{
		*OutCurrentIndex = INDEX_NONE;
	}
	if (!DataProvider.IsValid())
	{
		return nullptr;
	}
	const int32 CurrentIndex = DataProvider->ResolveCueIndex(CueIdentity);
	if (CurrentIndex == INDEX_NONE)
	{
		return nullptr;
	}
	if (OutCurrentIndex)
	{
		*OutCurrentIndex = CurrentIndex;
	}
	return DataProvider->ResolveCue(CueIdentity);
}

void SFrameEventEditor::ReconcileSelectedCueIdentity()
{
	int32 CurrentIndex = INDEX_NONE;
	UPaper2DPlusCueBase* CurrentCue = ResolveCueIdentity(SelectedCueIdentity, &CurrentIndex);
	if (!CurrentCue)
	{
		SelectedEventIndex = INDEX_NONE;
		SelectedCueIdentity = {};
		return;
	}
	SelectedEventIndex = CurrentIndex;
	// Refresh the fallback signature after Details/timeline edits. UObject identity is primary while the
	// placement survives; this current signature is what lets selection follow a later undo/reinstance.
	SelectedCueIdentity = DataProvider->GetCueIdentity(SelectedCueIdentity.Scope, CurrentCue);
}

#undef LOCTEXT_NAMESPACE
