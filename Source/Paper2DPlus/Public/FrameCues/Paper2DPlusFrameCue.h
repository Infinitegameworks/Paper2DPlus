// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Paper2DPlusNetTypes.h"
// The warming API below hands back soft Paper Flipbook references. CoreMinimal does not reach
// TSoftObjectPtr on 5.0-5.5; the generated companion happens to on 5.6+, so request it explicitly.
#include "UObject/SoftObjectPtr.h"
#include "Paper2DPlusFrameCue.generated.h"

class AActor;
class UPaperFlipbook;
class UPaperFlipbookComponent;
class UPaper2DPlusCharacterProfileAsset;
class UPaper2DPlusCharacterProfileComponent;
class USoundBase;
class UWorld;

/** The lifecycle phase delivered to a behavior-carrying Frame Cue placement and its listeners. */
UENUM(BlueprintType)
enum class EPaper2DPlusFrameCuePhase : uint8
{
	Trigger,
	Begin,
	Update,
	End
};

/** Why a Cue State ended. None is used for non-End notifications. */
UENUM(BlueprintType)
enum class EPaper2DPlusFrameCueEndReason : uint8
{
	None,
	Completed,
	AnimationChanged,
	Interrupted,
	SourceRemoved,
	LoopReset,
	PlaybackStopped,
	ComponentDestroyed,
	CueMutation,
	EditorReset,
	Forced
};

/** How this one Cue invocation was evaluated. RuntimePlayback is the legacy/default meaning. */
UENUM(BlueprintType)
enum class EPaper2DPlusFrameCueEvaluationMode : uint8
{
	RuntimePlayback = 0,
	RuntimeCatchUp,
	EditorPlayback,
	EditorScrubSeek UMETA(DisplayName = "Editor Scrub / Seek"),
	EditorSelectionPreview
};

/** The supported world-space origin for an effect spawned by Cue behavior. */
UENUM(BlueprintType)
enum class EPaper2DPlusFrameCueAnchorKind : uint8
{
	RenderOrigin = 0,
	ProfileSocket
};

/**
 * Which edge of its Trigger Frame fires a moment Cue.
 *
 * FrameStart is the legacy meaning: fire when playback enters or crosses the frame. FrameEnd
 * fires when the frame's TRAILING boundary is crossed — advancing past it, wrapping a loop over
 * it, or the animation naturally completing on it. A frame cut short (manual stop, animation
 * change, teardown) never completed, so its end-anchored cues deliberately do not fire.
 */
UENUM(BlueprintType)
enum class EPaper2DPlusCueTriggerEdge : uint8
{
	FrameStart = 0,
	FrameEnd
};

/** Attributable outcome from Resolve Frame Cue Anchor. Every failure resets both outputs. */
UENUM(BlueprintType)
enum class EPaper2DPlusFrameCueAnchorResult : uint8
{
	Success = 0,
	InvalidAnchorKind,
	InvalidPlaybackComponent,
	PlaybackComponentUnregistered,
	InvalidOwner,
	OwnerMismatch,
	WorldMismatch,
	MissingProfile,
	MissingFlipbook,
	MissingAnimation,
	ProfileRowNotFound,
	ProfileRowAmbiguous,
	FrameOutOfRange,
	MissingSocketName,
	SocketNotFound,
	SocketAmbiguous,
	PivotUnavailable,
	NonFiniteGeometry
};

/** Context broadcast with every Frame Cue lifecycle notification. */
USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FPaper2DPlusFrameCueContext
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Frame Cue")
	TObjectPtr<AActor> OwningActor = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Frame Cue")
	TObjectPtr<UPaper2DPlusCharacterProfileComponent> ProfileComponent = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Frame Cue")
	TObjectPtr<UPaperFlipbook> Flipbook = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Frame Cue")
	FName AnimationName;

	UPROPERTY(BlueprintReadOnly, Category = "Frame Cue")
	int32 CurrentFrame = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Frame Cue")
	int32 PreviousFrame = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Frame Cue")
	EPaper2DPlusFrameCuePhase Phase = EPaper2DPlusFrameCuePhase::Trigger;

	UPROPERTY(BlueprintReadOnly, Category = "Frame Cue")
	EPaper2DPlusFrameCueEndReason EndReason = EPaper2DPlusFrameCueEndReason::None;

	UPROPERTY(BlueprintReadOnly, Category = "Frame Cue")
	bool bWasLoopWrap = false;

	/** True when one transition crossed an entire Cue State and emitted its lifecycle together. */
	UPROPERTY(BlueprintReadOnly, Category = "Frame Cue")
	bool bIsCompressed = false;

	UPROPERTY(BlueprintReadOnly, Category = "Frame Cue")
	bool bIsEditorPreview = false;

	UPROPERTY(BlueprintReadOnly, Category = "Frame Cue")
	EPaper2DPlusNetContext NetContext = EPaper2DPlusNetContext::Standalone;

	UPROPERTY(BlueprintReadOnly, Category = "Frame Cue")
	bool bIsCatchUp = false;

	/** Exact Profile asset whose base/compiled row supplied this invocation snapshot. */
	UPROPERTY(BlueprintReadOnly, Category = "Frame Cue")
	TObjectPtr<UPaper2DPlusCharacterProfileAsset> CharacterProfile = nullptr;

	/** Live Paper render component that drives this timeline and may be used as an attachment parent. */
	UPROPERTY(BlueprintReadOnly, Category = "Frame Cue")
	TObjectPtr<UPaperFlipbookComponent> PlaybackComponent = nullptr;

	/** Explicit evaluation source. SetEvaluationMode keeps both legacy booleans in agreement. */
	UPROPERTY(BlueprintReadOnly, Category = "Frame Cue")
	EPaper2DPlusFrameCueEvaluationMode EvaluationMode =
		EPaper2DPlusFrameCueEvaluationMode::RuntimePlayback;

	void SetEvaluationMode(EPaper2DPlusFrameCueEvaluationMode InEvaluationMode)
	{
		EvaluationMode = InEvaluationMode;
		bIsEditorPreview = false;
		bIsCatchUp = false;

		switch (InEvaluationMode)
		{
		case EPaper2DPlusFrameCueEvaluationMode::RuntimePlayback:
			break;
		case EPaper2DPlusFrameCueEvaluationMode::RuntimeCatchUp:
			bIsCatchUp = true;
			break;
		case EPaper2DPlusFrameCueEvaluationMode::EditorPlayback:
		case EPaper2DPlusFrameCueEvaluationMode::EditorScrubSeek:
		case EPaper2DPlusFrameCueEvaluationMode::EditorSelectionPreview:
			bIsEditorPreview = true;
			break;
		default:
			EvaluationMode = EPaper2DPlusFrameCueEvaluationMode::RuntimePlayback;
			break;
		}
	}

	static FPaper2DPlusFrameCueContext MakePreview(
		int32 InCurrentFrame,
		int32 InPreviousFrame,
		bool bInWasLoopWrap,
		EPaper2DPlusFrameCueEvaluationMode InEvaluationMode =
			EPaper2DPlusFrameCueEvaluationMode::EditorPlayback)
	{
		FPaper2DPlusFrameCueContext Context;
		Context.CurrentFrame = InCurrentFrame;
		Context.PreviousFrame = InPreviousFrame;
		Context.bWasLoopWrap = bInWasLoopWrap;
		Context.SetEvaluationMode(InEvaluationMode);
		return Context;
	}
};

class UPaper2DPlusCueBase;
namespace Paper2DPlusFrameCueBehavior
{
	PAPER2DPLUS_API void ExecuteCueBehavior(
		UPaper2DPlusCueBase& Cue,
		const FPaper2DPlusFrameCueContext& Context);
}

/**
 * Instanced, reusable Frame Cue payload plus its own overridable behavior.
 *
 * A Cue Type declares payload fields and may implement the behavior events its lifecycle offers.
 * Behavior runs on the shared asset-owned placement instance, so it must stay stateless: read the
 * placement's payload and the context, act on the world, and never write instance state.
 */
UCLASS(Abstract, Hidden, BlueprintType, Blueprintable, EditInlineNew, DefaultToInstanced)
class PAPER2DPLUS_API UPaper2DPlusCueBase : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Resolves the triggering actor/component's world only while a Cue behavior callback is running.
	 *
	 * This is the same scoped-context pattern used by engine animation notifies: Blueprint world
	 * context nodes work from Cue `self`, while the shared asset-owned placement remains worldless
	 * before and after dispatch.
	 */
	virtual UWorld* GetWorld() const override;

#if WITH_EDITORONLY_DATA
	/**
	 * Tick to paint this placement's timeline bar in `Color` instead of resolving it from the Cue Tag
	 * Colors registry (and then the `Paper2DPlus.Cue.Default` convention).
	 *
	 * A separate flag rather than "any colour that is not the default": `Color` DEFAULTS to White, so
	 * treating white as unauthored made white the one colour a designer could not choose, and quietly
	 * swallowed near-white too (FLinearColor::Equals carries a tolerance). Nor can it be inferred after
	 * the fact — an already-saved placement sitting at the White default is indistinguishable from one
	 * deliberately set to white — so the intent has to be recorded explicitly.
	 *
	 * EDITOR-ONLY on purpose, for two independent reasons. `Color` has no runtime reader at all: the
	 * timeline is its only consumer, so nothing is lost from a cooked build. And the durable Cue Type
	 * schema hashes every non-editor-only, non-transient inherited property of this class into each
	 * type's canonical text — a persistent flag here would change every already-saved Cue Type's
	 * fingerprint and force a restage, which is a steep price for a timeline colour.
	 *
	 * MUST default to false: this is delta-serialized, so the class default is what every already-saved
	 * placement re-reads on load, and false keeps them resolving from the registry exactly as today.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cue")
	bool bOverrideColor = false;
#endif

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cue", meta = (EditCondition = "bOverrideColor"))
	FLinearColor Color = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cue")
	FName DebugName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cue", meta = (Categories = "Paper2DPlus.Cue"))
	FGameplayTag CueTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Networking")
	EPaper2DPlusFrameCueNetPolicy NetPolicy = EPaper2DPlusFrameCueNetPolicy::LocalAlways;

#if WITH_EDITORONLY_DATA
	/** Opt out of editor preview dispatch while keeping the Cue authorable and runtime-visible. */
	UPROPERTY(EditAnywhere, Category = "Preview")
	bool bSkipInEditorPreview = false;

	/** True when this placement was loaded from the retired executable Frame Event model. */
	UPROPERTY(VisibleAnywhere, Category = "Migration")
	bool bMigratedFromFrameEvent = false;

	/** Set after the project has wired the runtime receiver a migrated placement's payload needs. */
	UPROPERTY(EditAnywhere, Category = "Migration", meta = (EditCondition = "bMigratedFromFrameEvent"))
	bool bMigrationReceiverAcknowledged = false;
#endif

	/**
	 * Plays a sound for this cue, using whichever world the notification came from.
	 *
	 * In game the sound plays at the owning actor's location. While previewing in the Frame Cues tool
	 * the helper routes to editor-owned preview audio so seek/stop/close can silence it deterministically.
	 * Ordinary world-aware sound nodes also work during behavior through the isolated preview world.
	 * Dedicated servers play nothing.
	 * Returns true when the sound was routed somewhere audible.
	 */
	UFUNCTION(BlueprintCallable, Category = "Frame Cue",
		meta = (AdvancedDisplay = "VolumeMultiplier,PitchMultiplier"))
	bool PlayCueSound(
		const FPaper2DPlusFrameCueContext& Context,
		USoundBase* Sound,
		float VolumeMultiplier = 1.0f,
		float PitchMultiplier = 1.0f);

	/**
	 * Appends the effect art this placement will need, so an animation's art can be pulled in when
	 * the animation starts instead of hitching on the frame that finally spawns it.
	 *
	 * The base implementation structurally collects every scalar soft Paper Flipbook property on the
	 * placement. No Cue class or editor metadata is named, so the same cooked rule applies to every
	 * Cue Type. Override it only to add art the properties alone cannot describe, and call the base
	 * implementation first when you do.
	 */
	virtual void CollectWarmableEffectArt(
		TArray<TSoftObjectPtr<UPaperFlipbook>>& OutReferences) const;

	virtual bool IsRangeCue() const { return false; }
	virtual int32 GetPrimaryAnchorFrame() const { return INDEX_NONE; }
	virtual int32 GetCueFrameCount() const { return 1; }
	virtual bool ContainsFrame(int32 Frame) const { return Frame == GetPrimaryAnchorFrame(); }
	virtual bool ShouldEmitUpdates() const { return false; }

	/** Internal authoring mechanic; not a gameplay execution hook. */
	virtual bool RemapFrameAnchors(const TArray<int32>& OldToNew, int32 NumNewFrames) { return true; }
	virtual void SetPrimaryAnchorFrame(int32 NewFrame) {}

private:
	friend void Paper2DPlusFrameCueBehavior::ExecuteCueBehavior(
		UPaper2DPlusCueBase& Cue,
		const FPaper2DPlusFrameCueContext& Context);

	/** Scoped by ExecuteCueBehavior; never serialized and deliberately invalid outside that call. */
	TWeakObjectPtr<UObject> ActiveBehaviorWorldContext;
};

/** A Cue broadcasts Trigger once when its anchor is traversed. */
UCLASS(Abstract, BlueprintType, Blueprintable, meta = (DisplayName = "Cue"))
class PAPER2DPLUS_API UPaper2DPlusCue : public UPaper2DPlusCueBase
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Timing", meta = (ClampMin = "0"))
	int32 TriggerFrame = 0;

	/**
	 * Placement timing, like TriggerFrame: excluded from the durable Cue Type schema and edited
	 * on the timeline (the anchor diamond), never in Class Defaults. FrameStart preserves every
	 * existing placement's behavior through delta serialization.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Timing")
	EPaper2DPlusCueTriggerEdge TriggerEdge = EPaper2DPlusCueTriggerEdge::FrameStart;

	/**
	 * Runs once when playback crosses this cue's frame, immediately before listeners are notified.
	 * Read the placement's payload and the context; never store state on the cue itself.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "Frame Cue")
	void OnCueTriggered(const FPaper2DPlusFrameCueContext& Context);
	virtual void OnCueTriggered_Implementation(const FPaper2DPlusFrameCueContext& Context);

	virtual int32 GetPrimaryAnchorFrame() const override { return TriggerFrame; }
	virtual bool RemapFrameAnchors(const TArray<int32>& OldToNew, int32 NumNewFrames) override;
	virtual void SetPrimaryAnchorFrame(int32 NewFrame) override { TriggerFrame = NewFrame; }
};

/** A Cue State broadcasts Begin and End, plus Update when its cue class opts in. */
UCLASS(Abstract, BlueprintType, Blueprintable, meta = (DisplayName = "Cue State"))
class PAPER2DPLUS_API UPaper2DPlusCueState : public UPaper2DPlusCueBase
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Timing", meta = (ClampMin = "0"))
	int32 StartFrame = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Timing", meta = (ClampMin = "1"))
	int32 FrameCount = 1;

	/** Class-default lifecycle choice. Placements inherit it from their cue Blueprint. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Timing")
	bool bEmitUpdates = false;

	/**
	 * Runs once when playback enters this cue's range, immediately before listeners are notified.
	 * Every Begin is paired with an End, so start effects here and release them in On Cue End.
	 * Read the placement's payload and the context; never store state on the cue itself.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "Frame Cue")
	void OnCueBegin(const FPaper2DPlusFrameCueContext& Context);
	virtual void OnCueBegin_Implementation(const FPaper2DPlusFrameCueContext& Context);

	/**
	 * Runs on each frame inside the range while Emit Updates is enabled on this Cue Type.
	 * Read the placement's payload and the context; never store state on the cue itself.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "Frame Cue")
	void OnCueUpdate(const FPaper2DPlusFrameCueContext& Context);
	virtual void OnCueUpdate_Implementation(const FPaper2DPlusFrameCueContext& Context);

	/**
	 * Runs once when the range finishes or is cut short. Context End Reason says which, and a
	 * compressed traversal can deliver Begin and End on the same frame change. Keep whatever this
	 * releases on the receiving actor or component, never on the cue itself.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "Frame Cue")
	void OnCueEnd(const FPaper2DPlusFrameCueContext& Context);
	virtual void OnCueEnd_Implementation(const FPaper2DPlusFrameCueContext& Context);

	virtual bool IsRangeCue() const override { return true; }
	virtual int32 GetPrimaryAnchorFrame() const override { return StartFrame; }
	virtual int32 GetCueFrameCount() const override { return FMath::Max(1, FrameCount); }
	virtual bool ContainsFrame(int32 Frame) const override
	{
		return Frame >= StartFrame && Frame < StartFrame + GetCueFrameCount();
	}
	virtual bool ShouldEmitUpdates() const override { return bEmitUpdates; }
	virtual bool RemapFrameAnchors(const TArray<int32>& OldToNew, int32 NumNewFrames) override;
	virtual void SetPrimaryAnchorFrame(int32 NewFrame) override { StartFrame = NewFrame; }
};

/**
 * The single authority for which Cue behavior events exist.
 *
 * Every envelope layer (authoring contract, compiled generated-class allowlist, cook boundary) and
 * the restricted authoring surface read this list, so a new behavior event is declared once.
 */
namespace Paper2DPlusFrameCueBehavior
{
	/** Behavior events declared by the Cue base, in declaration order. */
	PAPER2DPLUS_API const TArray<FName>& GetMomentCueEventNames();

	/** Behavior events declared by the Cue State base, in declaration order. */
	PAPER2DPLUS_API const TArray<FName>& GetRangeCueEventNames();

	/** True for any name declared by a Paper2D+ Cue base as an overridable behavior event. */
	PAPER2DPLUS_API bool IsDeclaredEventName(FName FunctionName);

	/** Behavior events the supplied Cue class may override, in declaration order. */
	PAPER2DPLUS_API TArray<FName> GetDeclaredEventNamesForClass(const UClass* CueClass);

	/** Events a newly created Cue Type is seeded with so it opens onto an implementable graph. */
	PAPER2DPLUS_API TArray<FName> GetSeedEventNamesForClass(const UClass* CueClass);

	/**
	 * True when a placement can still be dispatched: the object is alive and its Cue Type class
	 * resolves. A placement orphaned by a deleted Cue Type fails here, is skipped by dispatch, and is
	 * reported by Character Profile validation.
	 */
	PAPER2DPLUS_API bool IsPlacementResolvable(const UPaper2DPlusCueBase* Cue);

	/**
	 * Runs one lifecycle notification's behavior on the placement itself.
	 *
	 * This is the single mapping from the internal Moment/Range timing kind plus lifecycle phase to
	 * the declared behavior event, shared by game dispatch and editor preview; it reads the context
	 * only, so it needs no profile component. Behavior runs on the SHARED asset-owned placement
	 * instance — two actors
	 * playing the same animation call it on the same object — so an implementation must stay
	 * stateless: read the placement's payload and the context, act on the world, never write
	 * instance state.
	 */
	PAPER2DPLUS_API void ExecuteCueBehavior(
		UPaper2DPlusCueBase& Cue,
		const FPaper2DPlusFrameCueContext& Context);

	/** Editor preview audio routing for the context-aware sound helper. */
	DECLARE_DELEGATE_RetVal_FourParams(
		bool,
		FPaper2DPlusFrameCuePreviewSound,
		const UPaper2DPlusCueBase& /*Cue*/,
		USoundBase* /*Sound*/,
		float /*VolumeMultiplier*/,
		float /*PitchMultiplier*/);

	/**
	 * Binds the editor preview audio path. The runtime module declares this seam and leaves it
	 * unbound; the editor module binds it at startup, so runtime never depends on editor code.
	 */
	PAPER2DPLUS_API void SetPreviewSoundHandler(FPaper2DPlusFrameCuePreviewSound InHandler);
	PAPER2DPLUS_API void ClearPreviewSoundHandler();
	PAPER2DPLUS_API bool IsPreviewSoundHandlerBound();

	/**
	 * A copy of whatever is bound right now.
	 *
	 * The seam is process-wide, so anything that swaps it temporarily can put the original back
	 * without knowing which module installed it.
	 */
	PAPER2DPLUS_API FPaper2DPlusFrameCuePreviewSound GetPreviewSoundHandler();

	/** Routes one preview sound request. Returns false when nothing is bound to hear it. */
	PAPER2DPLUS_API bool ExecutePreviewSound(
		const UPaper2DPlusCueBase& Cue,
		USoundBase* Sound,
		float VolumeMultiplier,
		float PitchMultiplier);
}

/** One optional designer-authored Cue track. The implicit Default track has no serialized definition. */
USTRUCT()
struct PAPER2DPLUS_API FPaper2DPlusFrameCueTrackDefinition
{
	GENERATED_BODY()

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	FGuid TrackId;

	UPROPERTY()
	FString DisplayName;
#endif
};

#if WITH_EDITOR
/** Explicit object replacement map used by editor copy/reinstance paths. */
using FPaper2DPlusFrameCueReplacementMap =
	TMap<const UPaper2DPlusCueBase*, UPaper2DPlusCueBase*>;
#endif

/**
 * Editor-only organization layered over an authoritative Cue array.
 *
 * No entry means Default. The Cue object is the placement identity; timing, payload, array order, semantic
 * digests, dispatch and cooked runtime never consult this sidecar. Mutating consumers should use the
 * storage-shape-neutral provider API instead of reaching into CueTrackIds directly.
 */
USTRUCT()
struct PAPER2DPLUS_API FPaper2DPlusFrameCueTrackLayout
{
	GENERATED_BODY()

#if WITH_EDITORONLY_DATA
	/** Ordered optional tracks. Default is implicit and can never appear here. */
	UPROPERTY()
	TArray<FPaper2DPlusFrameCueTrackDefinition> OptionalTracks;

	/** Preferred U1 identity shape: only non-Default Cue-to-track assignments are stored. */
	UPROPERTY()
	TMap<TObjectPtr<UPaper2DPlusCueBase>, FGuid> CueTrackIds;
#endif

#if WITH_EDITOR
	static FString NormalizeTrackName(const FString& InName);
	FString SuggestTrackName() const;
	bool IsTrackIdValid(const FGuid& TrackId) const;
	bool IsTrackNameAvailable(const FString& DisplayName, const FGuid& IgnoreTrackId = FGuid()) const;
	bool AddTrack(const FString& DisplayName, FGuid RequestedTrackId, FGuid& OutTrackId);
	bool RenameTrack(const FGuid& TrackId, const FString& DisplayName);
	bool ReorderTrack(const FGuid& TrackId, int32 NewOptionalTrackIndex);
	bool RemoveTrackDefinition(const FGuid& TrackId);

	/** Raw stored value; may name a missing/corrupt track and is therefore not safe for projection. */
	FGuid FindStoredTrackId(const UPaper2DPlusCueBase* Cue) const;
	/** Stored value only when its optional definition is unique and valid; otherwise Default. */
	FGuid ResolveStoredTrackId(const UPaper2DPlusCueBase* Cue) const;
	bool AssignCue(UPaper2DPlusCueBase* Cue, const FGuid& TrackId);
	void RemoveCue(UPaper2DPlusCueBase* Cue);
	int32 RemoveAssignmentsForTrack(const FGuid& TrackId);
	bool RemapCueReferences(const FPaper2DPlusFrameCueReplacementMap& Replacements);
	void RetainCueAssignments(const TSet<const UPaper2DPlusCueBase*>& ValidCues);
#endif
};

/**
 * Presentation-neutral severity emitted by the shared Frame Cue authoring/runtime-data validator.
 *
 * Every code currently defined below is an Error; Warning has no producer right now. It is kept
 * deliberately — this is a severity, not a code, and it is the seam a softer future code needs.
 */
enum class EPaper2DPlusFrameCueValidationSeverity : uint8
{
	Warning,
	Error
};

/** Stable reason codes let asset validators and bake preflight project one policy into their own issue types. */
enum class EPaper2DPlusFrameCueValidationCode : uint8
{
	NullCue,
	InvalidAnchor,
	InvalidRangeFrameCount,
	RangePastAnimationEnd,
	InvalidNetworkPolicy
};

struct PAPER2DPLUS_API FPaper2DPlusFrameCueValidationIssue
{
	EPaper2DPlusFrameCueValidationSeverity Severity = EPaper2DPlusFrameCueValidationSeverity::Error;
	EPaper2DPlusFrameCueValidationCode Code = EPaper2DPlusFrameCueValidationCode::NullCue;
	FString Message;
};

/**
 * The single structural policy for serialized Frame Cue placements.
 *
 * Intrinsic validation is animation-independent. Placement validation requires a definitive key-frame count;
 * a count of zero means no Cue can be placed. ValidateCue runs both passes and appends issues to OutIssues.
 */
namespace Paper2DPlusFrameCueValidation
{
	PAPER2DPLUS_API void ValidateIntrinsic(
		const UPaper2DPlusCueBase* Cue,
		TArray<FPaper2DPlusFrameCueValidationIssue>& OutIssues);

	PAPER2DPLUS_API void ValidatePlacement(
		const UPaper2DPlusCueBase* Cue,
		int32 KeyFrameCount,
		TArray<FPaper2DPlusFrameCueValidationIssue>& OutIssues);

	PAPER2DPLUS_API void ValidateCue(
		const UPaper2DPlusCueBase* Cue,
		int32 KeyFrameCount,
		TArray<FPaper2DPlusFrameCueValidationIssue>& OutIssues);
}
