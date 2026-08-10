// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ArrowComponent.h"
#include "Engine/World.h"
#include "FrameCues/Paper2DPlusFrameCue.h"
#include "GameFramework/Actor.h"
#include "Paper2DPlusCharacterProfileComponent.h"
#include "UObject/Script.h"
#include "UObject/Stack.h"

// 5.0-5.3 declare the Blueprint exception type inside Script.h; 5.4 moved it to its own header and
// left only a forward declaration behind, so the definition has to be requested explicitly there.
// The version macros must come from an explicit include, never PCH order.
#include "Misc/EngineVersionComparison.h"
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 4)
#include "Blueprint/BlueprintExceptionInfo.h"
#endif

#include "Sound/SoundBase.h"

#include "Paper2DPlusFrameCuePreviewBehaviorTestTypes.generated.h"

/**
 * Shared preview-behavior log.
 *
 * Deliberately not instance state: behavior runs on the shared asset-owned placement, so a cue that
 * recorded into itself would be the very contract violation these tests exist to catch.
 */
namespace Paper2DPlusPreviewBehaviorTestLog
{
	struct FRecord
	{
		const UPaper2DPlusCueBase* Cue = nullptr;
		EPaper2DPlusFrameCuePhase Phase = EPaper2DPlusFrameCuePhase::Trigger;
		EPaper2DPlusFrameCueEndReason EndReason = EPaper2DPlusFrameCueEndReason::None;
		int32 CurrentFrame = INDEX_NONE;
		int32 Payload = 0;
		bool bIsCompressed = false;
		bool bIsEditorPreview = false;
		bool bHadEditorPreviewWorld = false;
		bool bHadPreviewSubject = false;
		TWeakObjectPtr<UWorld> CueWorld;
		TWeakObjectPtr<AActor> OwningActor;
		TWeakObjectPtr<UPaper2DPlusCharacterProfileComponent> ProfileComponent;
		TWeakObjectPtr<UPaperFlipbook> Flipbook;
		FName AnimationName;
	};

	inline TArray<FRecord>& Records()
	{
		static TArray<FRecord> Log;
		return Log;
	}

	inline TArray<TWeakObjectPtr<AActor>>& SpawnedActors()
	{
		static TArray<TWeakObjectPtr<AActor>> Actors;
		return Actors;
	}

	inline void Reset()
	{
		Records().Reset();
		SpawnedActors().Reset();
	}

	inline void Add(
		const UPaper2DPlusCueBase& Cue,
		const FPaper2DPlusFrameCueContext& Context,
		int32 Payload)
	{
		FRecord& Record = Records().AddDefaulted_GetRef();
		Record.Cue = &Cue;
		Record.Phase = Context.Phase;
		Record.EndReason = Context.EndReason;
		Record.CurrentFrame = Context.CurrentFrame;
		Record.Payload = Payload;
		Record.bIsCompressed = Context.bIsCompressed;
		Record.bIsEditorPreview = Context.bIsEditorPreview;
		UWorld* WorldAtDispatch = Cue.GetWorld();
		Record.CueWorld = WorldAtDispatch;
		Record.bHadEditorPreviewWorld =
			IsValid(WorldAtDispatch)
			&& WorldAtDispatch->WorldType == EWorldType::EditorPreview;
		Record.bHadPreviewSubject =
			IsValid(Context.OwningActor.Get())
			&& IsValid(Context.ProfileComponent.Get());
		Record.OwningActor = Context.OwningActor;
		Record.ProfileComponent = Context.ProfileComponent;
		Record.Flipbook = Context.Flipbook;
		Record.AnimationName = Context.AnimationName;
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

	inline int32 CountPhaseForCue(const UPaper2DPlusCueBase* Cue, EPaper2DPlusFrameCuePhase Phase)
	{
		int32 Count = 0;
		for (const FRecord& Record : Records())
		{
			if (Record.Cue == Cue && Record.Phase == Phase)
			{
				++Count;
			}
		}
		return Count;
	}

	/**
	 * Fails the way a broken designer graph fails.
	 *
	 * A Blueprint that dereferences nothing, casts wrongly, or raises an error reports it on this exact
	 * channel, so driving the same channel keeps the containment test honest instead of inventing a
	 * private error convention that production code would never see.
	 */
	inline void RaiseBehaviorError(UObject& Object, const FName EventName)
	{
		UFunction* Function = Object.GetClass()->FindFunctionByName(EventName);
		if (!Function)
		{
			return;
		}
		FFrame StackFrame(&Object, Function, nullptr);
		const FBlueprintExceptionInfo ExceptionInfo(
			EBlueprintExceptionType::NonFatalError,
			FText::FromString(TEXT("Paper2DPlus preview behavior fixture failure")));
		FBlueprintCoreDelegates::ThrowScriptException(&Object, StackFrame, ExceptionInfo);
	}
}

/**
 * Cue fixture with real behavior. HideDropdown keeps native test fixtures out of every
 * designer-facing class picker.
 */
UCLASS(HideDropdown)
class UPaper2DPlusPreviewBehaviorMomentCueTest : public UPaper2DPlusCue
{
	GENERATED_BODY()

public:
	/** Per-placement payload the behavior reads, so two placements can be told apart. */
	UPROPERTY(EditAnywhere, Category = "Paper2DPlus Tests")
	int32 Payload = 0;

	/** Played through the context-aware sound helper, which routes to preview audio in the editor. */
	UPROPERTY(EditAnywhere, Category = "Paper2DPlus Tests")
	TObjectPtr<USoundBase> BehaviorSound = nullptr;

	UPROPERTY(EditAnywhere, Category = "Paper2DPlus Tests")
	float SoundVolume = 1.0f;

	UPROPERTY(EditAnywhere, Category = "Paper2DPlus Tests")
	float SoundPitch = 1.0f;

	/** When set, the behavior reports a Blueprint error exactly as a broken graph would. */
	UPROPERTY(EditAnywhere, Category = "Paper2DPlus Tests")
	bool bRaiseBehaviorError = false;

	/** Spawns an ordinary actor through Cue self's scoped world, without any preview adapter. */
	UPROPERTY(EditAnywhere, Category = "Paper2DPlus Tests")
	bool bSpawnActorInBehavior = false;

	virtual void OnCueTriggered_Implementation(
		const FPaper2DPlusFrameCueContext& Context) override
	{
		Paper2DPlusPreviewBehaviorTestLog::Add(*this, Context, Payload);
		if (bSpawnActorInBehavior)
		{
			if (UWorld* World = GetWorld())
			{
				if (AActor* Spawned = World->SpawnActor<AActor>())
				{
					Paper2DPlusPreviewBehaviorTestLog::SpawnedActors().Add(Spawned);
				}
			}
		}
		if (BehaviorSound)
		{
			PlayCueSound(Context, BehaviorSound, SoundVolume, SoundPitch);
		}
		if (bRaiseBehaviorError)
		{
			Paper2DPlusPreviewBehaviorTestLog::RaiseBehaviorError(*this, TEXT("OnCueTriggered"));
		}
	}
};

/**
 * A deliberately visible actor for the designer-Blueprint preview test.
 *
 * The test's Cue graph uses Unreal's ordinary Spawn Actor node; the arrow proves that the resulting
 * preview resource has registered renderable scene content rather than merely existing in a world.
 */
UCLASS(HideDropdown)
class APaper2DPlusPreviewBehaviorVisibleActorTest : public AActor
{
	GENERATED_BODY()

public:
	APaper2DPlusPreviewBehaviorVisibleActorTest();

	UPROPERTY(VisibleAnywhere, Category = "Paper2DPlus Tests")
	TObjectPtr<UArrowComponent> ArrowComponent;
};

/** Cue State fixture with real behavior on every declared lifecycle event. */
UCLASS(HideDropdown)
class UPaper2DPlusPreviewBehaviorRangeCueTest : public UPaper2DPlusCueState
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Paper2DPlus Tests")
	int32 Payload = 0;

	virtual void OnCueBegin_Implementation(
		const FPaper2DPlusFrameCueContext& Context) override
	{
		Paper2DPlusPreviewBehaviorTestLog::Add(*this, Context, Payload);
	}

	virtual void OnCueUpdate_Implementation(
		const FPaper2DPlusFrameCueContext& Context) override
	{
		Paper2DPlusPreviewBehaviorTestLog::Add(*this, Context, Payload);
	}

	virtual void OnCueEnd_Implementation(
		const FPaper2DPlusFrameCueContext& Context) override
	{
		Paper2DPlusPreviewBehaviorTestLog::Add(*this, Context, Payload);
	}
};
