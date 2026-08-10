// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FrameCues/Paper2DPlusFrameCue.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusCharacterProfileComponent.h"
#include "PaperFlipbook.h"
#include "PaperFlipbookComponent.h"
#include "Paper2DPlusTestFrameCueTypes.generated.h"

class UPaperFlipbook;
class UPaper2DPlusCharacterProfileAsset;

// TASK-173: every concrete fixture below carries meta=(Paper2DPlusAutomationFixture). The class
// stays fully Ready for FPaper2DPlusFrameCuePlacementAuthoring's commit gate (tests keep placing
// through the production path), but the two designer-facing pickers filter on the marker through
// FPaper2DPlusFrameCueTypeAuthoring::IsAutomationFixtureCueClass, so a Fab user's + Add Cue list
// never shows an automation type. Class metadata is UHT-portable across UE 5.0-5.8, unlike
// wrapping UCLASS declarations in WITH_DEV_AUTOMATION_TESTS.
UCLASS(meta = (Paper2DPlusAutomationFixture))
class UPaper2DPlusTestMomentCue : public UPaper2DPlusCue
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category="Paper2DPlus Tests")
	int32 CustomPayload = 0;
};

UCLASS(meta = (Paper2DPlusAutomationFixture))
class UPaper2DPlusTestRangeCue : public UPaper2DPlusCueState
{
	GENERATED_BODY()
};

/**
 * Shared behavior-execution log.
 *
 * Deliberately NOT instance state: behavior runs on the shared asset-owned placement, so a cue that
 * recorded into itself would be exactly the contract violation these tests exist to catch.
 */
namespace Paper2DPlusBehaviorTestLog
{
	struct FRecord
	{
		const UPaper2DPlusCueBase* Cue = nullptr;
		const AActor* OwningActor = nullptr;
		const UObject* ProfileComponent = nullptr;
		const UPaper2DPlusCharacterProfileAsset* CharacterProfile = nullptr;
		const UPaperFlipbookComponent* PlaybackComponent = nullptr;
		const UPaperFlipbook* Flipbook = nullptr;
		const UWorld* CueWorld = nullptr;
		EPaper2DPlusFrameCuePhase Phase = EPaper2DPlusFrameCuePhase::Trigger;
		EPaper2DPlusFrameCueEndReason EndReason = EPaper2DPlusFrameCueEndReason::None;
		EPaper2DPlusFrameCueEvaluationMode EvaluationMode =
			EPaper2DPlusFrameCueEvaluationMode::RuntimePlayback;
		FName AnimationName;
		int32 CurrentFrame = INDEX_NONE;
		int32 Payload = 0;
		bool bIsCompressed = false;
		bool bIsEditorPreview = false;
	};

	inline TArray<FRecord>& Records()
	{
		static TArray<FRecord> Log;
		return Log;
	}

	inline void Reset() { Records().Reset(); }

	inline void Add(
		const UPaper2DPlusCueBase& Cue,
		const FPaper2DPlusFrameCueContext& Context,
		int32 Payload)
	{
		FRecord& Record = Records().AddDefaulted_GetRef();
		Record.Cue = &Cue;
		Record.OwningActor = Context.OwningActor;
		Record.ProfileComponent = Context.ProfileComponent;
		Record.CharacterProfile = Context.CharacterProfile;
		Record.PlaybackComponent = Context.PlaybackComponent;
		Record.Flipbook = Context.Flipbook;
		Record.CueWorld = Cue.GetWorld();
		Record.Phase = Context.Phase;
		Record.EndReason = Context.EndReason;
		Record.EvaluationMode = Context.EvaluationMode;
		Record.AnimationName = Context.AnimationName;
		Record.CurrentFrame = Context.CurrentFrame;
		Record.Payload = Payload;
		Record.bIsCompressed = Context.bIsCompressed;
		Record.bIsEditorPreview = Context.bIsEditorPreview;
	}

	inline int32 CountPhase(EPaper2DPlusFrameCuePhase Phase)
	{
		int32 Count = 0;
		for (const FRecord& Record : Records())
		{
			if (Record.Phase == Phase)
			{
				++Count;
			}
		}
		return Count;
	}
}

/** A native Cue Type that implements the declared Moment behavior event. */
UCLASS(meta = (Paper2DPlusAutomationFixture))
class UPaper2DPlusTestBehaviorMomentCue : public UPaper2DPlusCue
{
	GENERATED_BODY()
public:
	/** Per-placement payload the behavior reads, so two placements can be told apart. */
	UPROPERTY(EditAnywhere, Category = "Paper2DPlus Tests")
	int32 Payload = 0;

	/** Optional playback mutation performed from inside behavior (the re-entrancy scenario). */
	UPROPERTY(EditAnywhere, Category = "Paper2DPlus Tests")
	TObjectPtr<UPaperFlipbook> MutateToFlipbook = nullptr;

	bool bReportPlaybackStopBeforeMutation = false;
	bool bReportPlaybackStopAfterMutation = false;

	virtual void OnCueTriggered_Implementation(
		const FPaper2DPlusFrameCueContext& Context) override
	{
		Paper2DPlusBehaviorTestLog::Add(*this, Context, Payload);
		const auto ReportPlaybackStop = [&Context]()
		{
			if (Context.ProfileComponent && Context.PlaybackComponent)
			{
				Context.ProfileComponent->ReportFrameCuePlaybackStopped(
					Context.PlaybackComponent,
					Context.ProfileComponent->GetFrameCuePlaybackGeneration());
			}
		};
		if (bReportPlaybackStopBeforeMutation)
		{
			ReportPlaybackStop();
		}
		if (MutateToFlipbook && Context.ProfileComponent)
		{
			if (UPaperFlipbookComponent* Component =
				Context.ProfileComponent->GetResolvedFlipbookComponent())
			{
				Component->SetFlipbook(MutateToFlipbook);
			}
			Context.ProfileComponent->HandleFlipbookChanged(MutateToFlipbook);
		}
		if (bReportPlaybackStopAfterMutation)
		{
			ReportPlaybackStop();
		}
	}
};

/** A native Cue Type that implements the declared Range behavior events. */
UCLASS(meta = (Paper2DPlusAutomationFixture))
class UPaper2DPlusTestBehaviorRangeCue : public UPaper2DPlusCueState
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "Paper2DPlus Tests")
	int32 Payload = 0;

	/** Reentrant terminal fixture: restart the source from inside End behavior. */
	bool bPlayFromStartOnEnd = false;

	virtual void OnCueBegin_Implementation(
		const FPaper2DPlusFrameCueContext& Context) override
	{
		Paper2DPlusBehaviorTestLog::Add(*this, Context, Payload);
	}

	virtual void OnCueUpdate_Implementation(
		const FPaper2DPlusFrameCueContext& Context) override
	{
		Paper2DPlusBehaviorTestLog::Add(*this, Context, Payload);
	}

	virtual void OnCueEnd_Implementation(
		const FPaper2DPlusFrameCueContext& Context) override
	{
		Paper2DPlusBehaviorTestLog::Add(*this, Context, Payload);
		if (bPlayFromStartOnEnd && Context.PlaybackComponent)
		{
			Context.PlaybackComponent->PlayFromStart();
		}
	}
};

/** A designer-shaped Cue Type with soft art plus a hard-reference control. */
UCLASS(meta = (Paper2DPlusAutomationFixture))
class UPaper2DPlusTestEffectArtCue : public UPaper2DPlusCue
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "Paper2DPlus Tests")
	TSoftObjectPtr<UPaperFlipbook> SoftArt;

	UPROPERTY(EditAnywhere, Category = "Paper2DPlus Tests")
	TObjectPtr<UPaperFlipbook> HardArt = nullptr;
};

UCLASS()
class UPaper2DPlusFrameCueRecorder : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY()
	TArray<TObjectPtr<UPaper2DPlusCueBase>> Cues;

	UPROPERTY()
	TArray<FPaper2DPlusFrameCueContext> Contexts;

	UPROPERTY()
	TArray<int32> WarmedEffectCountsAtDispatch;

	/** Behavior-log length observed when this listener ran — pins behavior-before-listener ordering. */
	UPROPERTY()
	TArray<int32> BehaviorLogSizesAtBroadcast;

	UFUNCTION()
	void OnCue(UPaper2DPlusCueBase* Cue, const FPaper2DPlusFrameCueContext& Context)
	{
		Cues.Add(Cue);
		Contexts.Add(Context);
		BehaviorLogSizesAtBroadcast.Add(Paper2DPlusBehaviorTestLog::Records().Num());
		// The seam is declared inside #if !UE_BUILD_SHIPPING on the component, so this call must
		// carry the same guard. BuildPlugin compiles a Shipping game configuration where the method
		// does not exist; the in-project editor build never does, which is why it stayed hidden.
#if !UE_BUILD_SHIPPING
		WarmedEffectCountsAtDispatch.Add(Context.ProfileComponent
			? Context.ProfileComponent->GetWarmedFrameCueEffectCountForTests()
			: INDEX_NONE);
#else
		WarmedEffectCountsAtDispatch.Add(INDEX_NONE);
#endif
	}
};

UCLASS()
class UPaper2DPlusFrameCueMutationReceiver : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY()
	TObjectPtr<UPaper2DPlusCueBase> TriggerCue = nullptr;

	UPROPERTY()
	EPaper2DPlusFrameCuePhase TriggerPhase = EPaper2DPlusFrameCuePhase::Trigger;

	UPROPERTY()
	TObjectPtr<UPaperFlipbook> TargetFlipbook = nullptr;

	UPROPERTY()
	TObjectPtr<UPaper2DPlusCharacterProfileAsset> TargetProfile = nullptr;

	UPROPERTY()
	TObjectPtr<UPaperFlipbookComponent> TargetPlaybackComponent = nullptr;

	int32 MutationCount = 0;
	bool bExplicitlyNotifyFlipbookChange = true;
	bool bBeginExternalPlayback = false;
	int64 ExternalBeginResult = INDEX_NONE;

	UFUNCTION()
	void OnCue(UPaper2DPlusCueBase* Cue, const FPaper2DPlusFrameCueContext& Context)
	{
		if (Cue != TriggerCue || Context.Phase != TriggerPhase || !Context.ProfileComponent)
		{
			return;
		}
		++MutationCount;
		if (TargetProfile)
		{
			Context.ProfileComponent->SetCharacterProfile(TargetProfile);
		}
		if (TargetFlipbook)
		{
			if (UPaperFlipbookComponent* Component = Context.ProfileComponent->GetResolvedFlipbookComponent())
			{
				Component->SetFlipbook(TargetFlipbook);
			}
			if (bExplicitlyNotifyFlipbookChange)
			{
				Context.ProfileComponent->HandleFlipbookChanged(TargetFlipbook);
			}
		}
		if (TargetPlaybackComponent)
		{
			Context.ProfileComponent->SetFrameCuePlaybackSource(TargetPlaybackComponent);
		}
		if (bBeginExternalPlayback && Context.PlaybackComponent)
		{
			ExternalBeginResult = Context.ProfileComponent->BeginExternalFrameCuePlayback(
				Context.PlaybackComponent);
		}
	}
};

/** Mutates a stock playback source from a later OnFinishedPlaying listener. */
UCLASS()
class UPaper2DPlusPlaybackFinishMutationReceiver : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY()
	TObjectPtr<UPaperFlipbookComponent> PlaybackComponent = nullptr;

	UPROPERTY()
	TObjectPtr<UPaperFlipbook> TargetFlipbook = nullptr;

	bool bRestartFromStart = false;
	bool bDestroyComponent = false;
	int32 InvocationCount = 0;

	UFUNCTION()
	void OnFinishedPlaying()
	{
		++InvocationCount;
		if (!PlaybackComponent)
		{
			return;
		}
		if (TargetFlipbook)
		{
			PlaybackComponent->SetFlipbook(TargetFlipbook);
		}
		if (bRestartFromStart)
		{
			PlaybackComponent->PlayFromStart();
		}
		if (bDestroyComponent)
		{
			PlaybackComponent->DestroyComponent();
		}
	}
};
