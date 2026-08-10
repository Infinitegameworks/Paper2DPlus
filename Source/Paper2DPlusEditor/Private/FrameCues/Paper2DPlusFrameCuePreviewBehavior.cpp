// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "FrameCues/Paper2DPlusFrameCuePreviewBehavior.h"

#include "Editor.h"
#include "FrameCues/Paper2DPlusFrameCue.h"
#include "FrameCues/Paper2DPlusFrameCuePreviewContext.h"
#include "FrameCues/Paper2DPlusFrameCuePreviewHost.h"
#include "Paper2DPlusModule.h"
#include "Sound/SoundBase.h"
#include "UObject/ObjectKey.h"
#include "UObject/Script.h"

// 5.0-5.3 declare the Blueprint exception type inside Script.h; 5.4 moved it to its own header and
// left only a forward declaration behind, so the definition has to be requested explicitly there.
// The version macros must come from an explicit include, never PCH order.
#include "Misc/EngineVersionComparison.h"
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 4)
#include "Blueprint/BlueprintExceptionInfo.h"
#endif

#define LOCTEXT_NAMESPACE "Paper2DPlusFrameCuePreviewBehavior"

namespace Paper2DPlusFrameCuePreviewBehaviorInternal
{
	/**
	 * Session state.
	 *
	 * Keyed by placement, not by Cue Type: the badge marks the placement the designer sees on the
	 * timeline, and one broken placement must not silence the report for a sibling placement.
	 */
	TMap<FObjectKey, FText> ErroredPlacements;

	/** Preview context owning the audio ledger for the behavior currently executing, if any. */
	TWeakObjectPtr<UPaper2DPlusFrameCuePreviewContext> ActiveAudioContext;

#if WITH_DEV_AUTOMATION_TESTS
	int32 PreviewSoundRequestCount = 0;
	Paper2DPlusFrameCuePreviewBehavior::FPreviewSoundRequestRecord LastPreviewSoundRequest;
#endif

	/** Routes context-aware sound helper calls made by behavior to this preview's owned ledger. */
	struct FScopedPreviewAudioRoute
	{
		explicit FScopedPreviewAudioRoute(UPaper2DPlusFrameCuePreviewContext* Context)
			: Previous(ActiveAudioContext)
		{
			ActiveAudioContext = Context;
		}

		~FScopedPreviewAudioRoute()
		{
			ActiveAudioContext = Previous;
		}

		TWeakObjectPtr<UPaper2DPlusFrameCuePreviewContext> Previous;
	};

	bool IsErrorException(const EBlueprintExceptionType::Type ExceptionType)
	{
		switch (ExceptionType)
		{
		case EBlueprintExceptionType::Breakpoint:
		case EBlueprintExceptionType::Tracepoint:
		case EBlueprintExceptionType::WireTracepoint:
			return false;
		default:
			return true;
		}
	}

	/**
	 * Captures the Blueprint script errors raised while one behavior event runs.
	 *
	 * This is the same channel the VM reports an access-none, a failed cast, or a raised error on, so
	 * a broken designer graph is observed exactly as the engine reports it rather than through a
	 * parallel error convention.
	 */
	class FScopedBehaviorErrorCapture
	{
	public:
		FScopedBehaviorErrorCapture()
		{
			Handle = FBlueprintCoreDelegates::OnScriptException.AddRaw(
				this, &FScopedBehaviorErrorCapture::HandleScriptException);
		}

		~FScopedBehaviorErrorCapture()
		{
			FBlueprintCoreDelegates::OnScriptException.Remove(Handle);
		}

		bool HasError() const { return bHasError; }
		const FText& GetMessage() const { return Message; }

	private:
		void HandleScriptException(
			const UObject* ActiveObject,
			const FFrame& StackFrame,
			const FBlueprintExceptionInfo& Info)
		{
			if (bHasError || !IsErrorException(Info.GetType()))
			{
				return;
			}
			bHasError = true;
			Message = Info.GetDescription();
		}

		FDelegateHandle Handle;
		FText Message;
		bool bHasError = false;
	};

	FText MakeBadgeToolTip(const FText& Message)
	{
		return FText::Format(
			LOCTEXT(
				"CueBehaviorPreviewErrorBadge",
				"This placement's behavior failed during preview: {0}\nPreview keeps running; open the Cue Type and fix its behavior graph."),
			Message);
	}

	/** Drop badges whose placement has been garbage collected, so a long session cannot accumulate
	 *  entries for objects that no longer exist. FObjectKey never dangles, but it also never notices
	 *  the object died, so nothing else would ever remove them. */
	void PruneDeadPlacementBadges()
	{
		for (auto It = ErroredPlacements.CreateIterator(); It; ++It)
		{
			if (It.Key().ResolveObjectPtr() == nullptr)
			{
				It.RemoveCurrent();
			}
		}
	}

	void ClearBehaviorError(const UPaper2DPlusCueBase& Cue)
	{
		if (ErroredPlacements.Remove(FObjectKey(&Cue)) > 0)
		{
			// Retiring a badge re-arms the log for this placement: the next genuine failure is worth
			// saying out loud again, because it is a NEW failure rather than a repeat of the old one.
			PruneDeadPlacementBadges();
		}
	}

	void NoteBehaviorError(const UPaper2DPlusCueBase& Cue, const FText& Message)
	{
		const FObjectKey PlacementKey(&Cue);
		if (ErroredPlacements.Contains(PlacementKey))
		{
			// Already reported for this editor session. The badge stays, the log stays quiet, and the
			// behavior is deliberately NOT quarantined — the next dispatch runs it again.
			return;
		}
		ErroredPlacements.Add(PlacementKey, MakeBadgeToolTip(Message));
		UE_LOG(
			LogPaper2DPlus,
			Error,
			TEXT("Frame Cue behavior failed in editor preview. Placement '%s' of Cue Type '%s' reported: %s. ")
			TEXT("Preview continues and the placement is badged in the Frame Cues timeline. ")
			TEXT("This is reported once per placement per editor session."),
			*Cue.GetPathName(),
			*GetPathNameSafe(Cue.GetClass()),
			*Message.ToString());
	}

	bool HandlePreviewSound(
		const UPaper2DPlusCueBase& Cue,
		USoundBase* Sound,
		const float VolumeMultiplier,
		const float PitchMultiplier)
	{
		if (!Sound)
		{
			return false;
		}

		UPaper2DPlusFrameCuePreviewContext* Context = ActiveAudioContext.Get();
		bool bRoutedToPreviewLedger = false;
		bool bPlayed = false;
		if (Context)
		{
			// Ledger-owned, so scrub-back, stop, tool switch, and tab close silence the sound with
			// every other preview resource instead of leaving it playing over the next animation.
			bRoutedToPreviewLedger =
				Context->PlaySound(Sound, VolumeMultiplier, PitchMultiplier, 0.0f).IsValid();
			bPlayed = bRoutedToPreviewLedger;
		}
		else
		{
			// Outside a preview dispatch there is no ledger to own the sound. The editor's shared
			// audition channel remains the worldless fallback.
			bPlayed = GEditor && GEditor->PlayPreviewSound(Sound) != nullptr;
		}
#if WITH_DEV_AUTOMATION_TESTS
		++PreviewSoundRequestCount;
		LastPreviewSoundRequest.Cue = &Cue;
		LastPreviewSoundRequest.Sound = Sound;
		LastPreviewSoundRequest.VolumeMultiplier = VolumeMultiplier;
		LastPreviewSoundRequest.PitchMultiplier = PitchMultiplier;
		LastPreviewSoundRequest.bRoutedToPreviewLedger = bRoutedToPreviewLedger;
#endif
		return bPlayed;
	}

	void ExecuteBehaviorContained(
		UPaper2DPlusCueBase& Cue,
		const FPaper2DPlusFrameCueContext& Context,
		UPaper2DPlusFrameCuePreviewContext* AudioContext)
	{
		if (!Paper2DPlusFrameCueBehavior::IsPlacementResolvable(&Cue))
		{
			// A placement orphaned by a deleted Cue Type can neither run behavior nor serve payload.
			// Its preview adapters still receive the notification so an active range is closed.
			return;
		}

		FScopedPreviewAudioRoute AudioRoute(AudioContext);
		FScopedBehaviorErrorCapture ErrorCapture;
		Paper2DPlusFrameCueBehavior::ExecuteCueBehavior(Cue, Context);
		if (ErrorCapture.HasError())
		{
			NoteBehaviorError(Cue, ErrorCapture.GetMessage());
		}
		else
		{
			// A clean run retires the badge. Most preview failures are DATA problems on the placement
			// (a payload variable left at zero), which the designer fixes without recompiling the Cue
			// Type — so the placement keeps its identity and, without this, kept a red badge and a stale
			// message describing a failure that no longer happens until the editor was restarted.
			ClearBehaviorError(Cue);
		}
	}
}

void Paper2DPlusFrameCuePreviewBehavior::NotifyPreview(
	UPaper2DPlusCueBase& Cue,
	const FPaper2DPlusFrameCueContext& Context,
	FPaper2DPlusFrameCuePreviewHost* PreviewHost)
{
	Paper2DPlusFrameCuePreviewBehaviorInternal::ExecuteBehaviorContained(
		Cue,
		Context,
		PreviewHost ? PreviewHost->GetContext() : nullptr);

	if (PreviewHost)
	{
		PreviewHost->Notify(&Cue, Context);
	}
}

void Paper2DPlusFrameCuePreviewBehavior::RegisterPreviewSoundHandler()
{
	using namespace Paper2DPlusFrameCuePreviewBehaviorInternal;
	// Deliberately unconditional: the seam holds one handler, so installing the editor's again simply
	// replaces it. Keeping no registration flag means anything that borrowed the seam and released it
	// cannot leave preview permanently mute.
	Paper2DPlusFrameCueBehavior::SetPreviewSoundHandler(
		Paper2DPlusFrameCueBehavior::FPaper2DPlusFrameCuePreviewSound::CreateStatic(
			&HandlePreviewSound));
}

void Paper2DPlusFrameCuePreviewBehavior::UnregisterPreviewSoundHandler()
{
	using namespace Paper2DPlusFrameCuePreviewBehaviorInternal;
	Paper2DPlusFrameCueBehavior::ClearPreviewSoundHandler();
	ActiveAudioContext.Reset();
}

Paper2DPlusFrameCuePreviewBehavior::FPlacementBadge
Paper2DPlusFrameCuePreviewBehavior::GetPlacementBadge(const UPaper2DPlusCueBase* Cue)
{
	FPlacementBadge Badge;
	if (!Cue)
	{
		return Badge;
	}
	if (const FText* ToolTip =
		Paper2DPlusFrameCuePreviewBehaviorInternal::ErroredPlacements.Find(FObjectKey(Cue)))
	{
		Badge.bHasError = true;
		Badge.ToolTip = *ToolTip;
	}
	return Badge;
}

void Paper2DPlusFrameCuePreviewBehavior::SeedVisualTourPlacementErrorBadge(
	const UPaper2DPlusCueBase& Cue,
	const FText& Message)
{
	using namespace Paper2DPlusFrameCuePreviewBehaviorInternal;
	ErroredPlacements.Add(FObjectKey(&Cue), MakeBadgeToolTip(Message));
}

void Paper2DPlusFrameCuePreviewBehavior::ClearVisualTourPlacementErrorBadge(
	const UPaper2DPlusCueBase& Cue)
{
	Paper2DPlusFrameCuePreviewBehaviorInternal::ErroredPlacements.Remove(FObjectKey(&Cue));
}

#if WITH_DEV_AUTOMATION_TESTS
void Paper2DPlusFrameCuePreviewBehavior::ResetSessionForTests()
{
	using namespace Paper2DPlusFrameCuePreviewBehaviorInternal;
	ErroredPlacements.Reset();
	PreviewSoundRequestCount = 0;
	LastPreviewSoundRequest = FPreviewSoundRequestRecord();
}

int32 Paper2DPlusFrameCuePreviewBehavior::GetPreviewSoundRequestCountForTests()
{
	return Paper2DPlusFrameCuePreviewBehaviorInternal::PreviewSoundRequestCount;
}

Paper2DPlusFrameCuePreviewBehavior::FPreviewSoundRequestRecord
Paper2DPlusFrameCuePreviewBehavior::GetLastPreviewSoundRequestForTests()
{
	return Paper2DPlusFrameCuePreviewBehaviorInternal::LastPreviewSoundRequest;
}

int32 Paper2DPlusFrameCuePreviewBehavior::GetBadgedPlacementCountForTests()
{
	return Paper2DPlusFrameCuePreviewBehaviorInternal::ErroredPlacements.Num();
}
#endif

#undef LOCTEXT_NAMESPACE
