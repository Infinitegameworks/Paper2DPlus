// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "FrameCues/Paper2DPlusFrameCue.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"
#include "Paper2DPlusCharacterProfileComponent.h"
#include "PaperFlipbook.h"
#include "Sound/SoundBase.h"
#include "UObject/UnrealType.h"

namespace Paper2DPlusFrameCueBehaviorInternal
{
	/** Unbound in game and in cooked builds; the editor module binds it for Frame Cues preview. */
	Paper2DPlusFrameCueBehavior::FPaper2DPlusFrameCuePreviewSound PreviewSoundHandler;
}

namespace
{
	void AddCueValidationIssue(
		TArray<FPaper2DPlusFrameCueValidationIssue>& OutIssues,
		EPaper2DPlusFrameCueValidationSeverity Severity,
		EPaper2DPlusFrameCueValidationCode Code,
		FString Message)
	{
		FPaper2DPlusFrameCueValidationIssue& Issue = OutIssues.AddDefaulted_GetRef();
		Issue.Severity = Severity;
		Issue.Code = Code;
		Issue.Message = MoveTemp(Message);
	}
}

void Paper2DPlusFrameCueValidation::ValidateIntrinsic(
	const UPaper2DPlusCueBase* Cue,
	TArray<FPaper2DPlusFrameCueValidationIssue>& OutIssues)
{
	if (!Cue)
	{
		AddCueValidationIssue(
			OutIssues,
			EPaper2DPlusFrameCueValidationSeverity::Error,
			EPaper2DPlusFrameCueValidationCode::NullCue,
			TEXT("Frame Cue slot is null. Remove it or assign a Cue class in the Frame Cues timeline."));
		return;
	}

	const int32 AnchorFrame = Cue->GetPrimaryAnchorFrame();
	if (AnchorFrame < 0)
	{
		AddCueValidationIssue(
			OutIssues,
			EPaper2DPlusFrameCueValidationSeverity::Error,
			EPaper2DPlusFrameCueValidationCode::InvalidAnchor,
			FString::Printf(TEXT("Cue anchor %d is negative."), AnchorFrame));
	}

	if (const UPaper2DPlusCueState* RangeCue = Cast<UPaper2DPlusCueState>(Cue))
	{
		if (RangeCue->FrameCount < 1)
		{
			AddCueValidationIssue(
				OutIssues,
				EPaper2DPlusFrameCueValidationSeverity::Error,
				EPaper2DPlusFrameCueValidationCode::InvalidRangeFrameCount,
				TEXT("Cue State FrameCount must be at least 1."));
		}
	}

	const UEnum* NetPolicyEnum = StaticEnum<EPaper2DPlusFrameCueNetPolicy>();
	if (!NetPolicyEnum || !NetPolicyEnum->IsValidEnumValue(static_cast<int64>(Cue->NetPolicy)))
	{
		AddCueValidationIssue(
			OutIssues,
			EPaper2DPlusFrameCueValidationSeverity::Error,
			EPaper2DPlusFrameCueValidationCode::InvalidNetworkPolicy,
			TEXT("Frame Cue has an invalid network policy value."));
	}
}

void Paper2DPlusFrameCueValidation::ValidatePlacement(
	const UPaper2DPlusCueBase* Cue,
	int32 KeyFrameCount,
	TArray<FPaper2DPlusFrameCueValidationIssue>& OutIssues)
{
	if (!Cue || KeyFrameCount < 0)
	{
		return;
	}

	const int32 AnchorFrame = Cue->GetPrimaryAnchorFrame();
	if (AnchorFrame < 0)
	{
		return; // ValidateIntrinsic owns the one negative-anchor diagnostic.
	}
	if (AnchorFrame >= KeyFrameCount)
	{
		AddCueValidationIssue(
			OutIssues,
			EPaper2DPlusFrameCueValidationSeverity::Error,
			EPaper2DPlusFrameCueValidationCode::InvalidAnchor,
			FString::Printf(TEXT("Cue anchor %d is out of bounds for %d authored key frames."),
				AnchorFrame, KeyFrameCount));
		return;
	}

	const UPaper2DPlusCueState* RangeCue = Cast<UPaper2DPlusCueState>(Cue);
	if (!RangeCue || RangeCue->FrameCount < 1)
	{
		return; // ValidateIntrinsic owns invalid duration; no trustworthy span exists yet.
	}

	const int64 EndFrameInclusive = static_cast<int64>(AnchorFrame)
		+ static_cast<int64>(RangeCue->FrameCount) - 1;
	if (EndFrameInclusive >= static_cast<int64>(KeyFrameCount))
	{
		AddCueValidationIssue(
			OutIssues,
			EPaper2DPlusFrameCueValidationSeverity::Error,
			EPaper2DPlusFrameCueValidationCode::RangePastAnimationEnd,
			FString::Printf(TEXT("Cue State ends at frame %lld, past the animation's last frame %d."),
				EndFrameInclusive, KeyFrameCount - 1));
	}
}

void Paper2DPlusFrameCueValidation::ValidateCue(
	const UPaper2DPlusCueBase* Cue,
	int32 KeyFrameCount,
	TArray<FPaper2DPlusFrameCueValidationIssue>& OutIssues)
{
	ValidateIntrinsic(Cue, OutIssues);
	ValidatePlacement(Cue, KeyFrameCount, OutIssues);
}

const TArray<FName>& Paper2DPlusFrameCueBehavior::GetMomentCueEventNames()
{
	static const TArray<FName> Names = { FName(TEXT("OnCueTriggered")) };
	return Names;
}

const TArray<FName>& Paper2DPlusFrameCueBehavior::GetRangeCueEventNames()
{
	static const TArray<FName> Names = {
		FName(TEXT("OnCueBegin")),
		FName(TEXT("OnCueUpdate")),
		FName(TEXT("OnCueEnd"))
	};
	return Names;
}

bool Paper2DPlusFrameCueBehavior::IsDeclaredEventName(const FName FunctionName)
{
	return GetMomentCueEventNames().Contains(FunctionName)
		|| GetRangeCueEventNames().Contains(FunctionName);
}

TArray<FName> Paper2DPlusFrameCueBehavior::GetDeclaredEventNamesForClass(const UClass* CueClass)
{
	if (!CueClass)
	{
		return {};
	}
	if (CueClass->IsChildOf(UPaper2DPlusCueState::StaticClass()))
	{
		return GetRangeCueEventNames();
	}
	if (CueClass->IsChildOf(UPaper2DPlusCue::StaticClass()))
	{
		return GetMomentCueEventNames();
	}
	return {};
}

TArray<FName> Paper2DPlusFrameCueBehavior::GetSeedEventNamesForClass(const UClass* CueClass)
{
	// Update is opt-in per Cue Type, so a fresh Cue State is seeded with its guaranteed pair only.
	static const FName OnCueUpdateName(TEXT("OnCueUpdate"));
	TArray<FName> Seeds = GetDeclaredEventNamesForClass(CueClass);
	Seeds.Remove(OnCueUpdateName);
	return Seeds;
}

bool Paper2DPlusFrameCueBehavior::IsPlacementResolvable(const UPaper2DPlusCueBase* Cue)
{
	// IsValid alone is not liveness here: an object the GC's reachability pass has already marked
	// is not pending-kill yet, but ProcessEvent on it asserts (!IsUnreachable). Composed Layer
	// views and range ledgers are deliberately GC-invisible, so a cue can reach dispatch in
	// exactly that window - UE 5.0-5.4 run that pass between automation tests.
	if (!IsValid(Cue) || Cue->IsUnreachable())
	{
		return false;
	}

	// A deleted Cue Type leaves its placements pointing at a class that is gone or has been replaced
	// by a reinstancing shell. Neither can run behavior or serve payload, so dispatch treats them the
	// way it treats a missing placement and validation reports them.
	const UClass* CueClass = Cue->GetClass();
	return IsValid(CueClass) && !CueClass->HasAnyClassFlags(CLASS_NewerVersionExists);
}

UWorld* UPaper2DPlusCueBase::GetWorld() const
{
	// Do not call Super::GetWorld() here. UObject::ImplementsGetWorld uses the native override to
	// decide whether Blueprint world-context nodes are legal, and the base implementation can mark
	// that probe as unsupported while walking from this asset-owned object to its package.
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		return nullptr;
	}
	UObject* WorldContext = ActiveBehaviorWorldContext.Get();
	return WorldContext ? WorldContext->GetWorld() : nullptr;
}

void Paper2DPlusFrameCueBehavior::ExecuteCueBehavior(
	UPaper2DPlusCueBase& Cue,
	const FPaper2DPlusFrameCueContext& Context)
{
	if (!IsPlacementResolvable(&Cue))
	{
		return;
	}

	// Cue placements are shared by every actor using the Profile, so they cannot permanently own a
	// world. Install the triggering component/actor only for this synchronous event, then restore the
	// prior value for nested/re-entrant dispatch. Latent behavior is intentionally outside the Cue
	// contract; world-created actors/components/timers own their own lifetime after this call returns.
	UObject* DispatchWorldContext = nullptr;
	if (UPaper2DPlusCharacterProfileComponent* ProfileComponent = Context.ProfileComponent.Get();
		IsValid(ProfileComponent) && ProfileComponent->GetWorld())
	{
		DispatchWorldContext = ProfileComponent;
	}
	else if (AActor* OwningActor = Context.OwningActor.Get(); IsValid(OwningActor))
	{
		DispatchWorldContext = OwningActor;
	}
	const TWeakObjectPtr<UObject> ScopedWorldContext(DispatchWorldContext);
	TGuardValue<TWeakObjectPtr<UObject>> WorldContextGuard(
		Cue.ActiveBehaviorWorldContext,
		ScopedWorldContext);

	switch (Context.Phase)
	{
	case EPaper2DPlusFrameCuePhase::Trigger:
		if (UPaper2DPlusCue* MomentCue = Cast<UPaper2DPlusCue>(&Cue))
		{
			MomentCue->OnCueTriggered(Context);
		}
		break;

	case EPaper2DPlusFrameCuePhase::Begin:
		if (UPaper2DPlusCueState* RangeCue = Cast<UPaper2DPlusCueState>(&Cue))
		{
			RangeCue->OnCueBegin(Context);
		}
		break;

	case EPaper2DPlusFrameCuePhase::Update:
		if (UPaper2DPlusCueState* RangeCue = Cast<UPaper2DPlusCueState>(&Cue))
		{
			RangeCue->OnCueUpdate(Context);
		}
		break;

	case EPaper2DPlusFrameCuePhase::End:
		if (UPaper2DPlusCueState* RangeCue = Cast<UPaper2DPlusCueState>(&Cue))
		{
			RangeCue->OnCueEnd(Context);
		}
		break;

	default:
		break;
	}
}

void Paper2DPlusFrameCueBehavior::SetPreviewSoundHandler(
	FPaper2DPlusFrameCuePreviewSound InHandler)
{
	Paper2DPlusFrameCueBehaviorInternal::PreviewSoundHandler = MoveTemp(InHandler);
}

void Paper2DPlusFrameCueBehavior::ClearPreviewSoundHandler()
{
	Paper2DPlusFrameCueBehaviorInternal::PreviewSoundHandler.Unbind();
}

bool Paper2DPlusFrameCueBehavior::IsPreviewSoundHandlerBound()
{
	return Paper2DPlusFrameCueBehaviorInternal::PreviewSoundHandler.IsBound();
}

Paper2DPlusFrameCueBehavior::FPaper2DPlusFrameCuePreviewSound
Paper2DPlusFrameCueBehavior::GetPreviewSoundHandler()
{
	return Paper2DPlusFrameCueBehaviorInternal::PreviewSoundHandler;
}

bool Paper2DPlusFrameCueBehavior::ExecutePreviewSound(
	const UPaper2DPlusCueBase& Cue,
	USoundBase* Sound,
	const float VolumeMultiplier,
	const float PitchMultiplier)
{
	if (!Sound || !Paper2DPlusFrameCueBehaviorInternal::PreviewSoundHandler.IsBound())
	{
		return false;
	}
	return Paper2DPlusFrameCueBehaviorInternal::PreviewSoundHandler.Execute(
		Cue, Sound, VolumeMultiplier, PitchMultiplier);
}

bool UPaper2DPlusCueBase::PlayCueSound(
	const FPaper2DPlusFrameCueContext& Context,
	USoundBase* Sound,
	const float VolumeMultiplier,
	const float PitchMultiplier)
{
	if (!Sound)
	{
		return false;
	}

	if (Context.bIsEditorPreview)
	{
		return Paper2DPlusFrameCueBehavior::ExecutePreviewSound(
			*this, Sound, VolumeMultiplier, PitchMultiplier);
	}

	// No actor means no world and no listener position; the caller gets an honest false rather than a
	// silent no-op that looks like a broken sound asset.
	const AActor* Actor = Context.OwningActor;
	const UWorld* World = Actor ? Actor->GetWorld() : nullptr;
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return false;
	}

	UGameplayStatics::PlaySoundAtLocation(
		Actor,
		Sound,
		Actor->GetActorLocation(),
		VolumeMultiplier,
		PitchMultiplier);
	return true;
}

void UPaper2DPlusCueBase::CollectWarmableEffectArt(
	TArray<TSoftObjectPtr<UPaperFlipbook>>& OutReferences) const
{
	const UClass* CueClass = GetClass();
	if (!CueClass)
	{
		return;
	}

	for (TFieldIterator<FProperty> PropertyIt(CueClass, EFieldIteratorFlags::IncludeSuper);
		PropertyIt;
		++PropertyIt)
	{
		const FProperty* Property = *PropertyIt;

		// A softly stored Paper Flipbook is the case warming exists for: the placement loads without
		// its art, so something has to pull the art in before the frame that spawns it. The test is
		// structural rather than metadata-driven on purpose — a cooked build carries no property
		// metadata at all, and the no-hitch contract has to hold there too.
		if (const FSoftObjectProperty* SoftProperty = CastField<FSoftObjectProperty>(Property))
		{
			if (!SoftProperty->PropertyClass
				|| !SoftProperty->PropertyClass->IsChildOf(UPaperFlipbook::StaticClass()))
			{
				continue;
			}
			const FSoftObjectPtr& Value = SoftProperty->GetPropertyValue_InContainer(this);
			if (UPaperFlipbook* AlreadyResolved = Cast<UPaperFlipbook>(Value.Get()))
			{
				OutReferences.AddUnique(TSoftObjectPtr<UPaperFlipbook>(AlreadyResolved));
			}
			else if (!Value.ToSoftObjectPath().IsNull())
			{
				OutReferences.AddUnique(
					TSoftObjectPtr<UPaperFlipbook>(Value.ToSoftObjectPath()));
			}
			continue;
		}
	}
}

void UPaper2DPlusCue::OnCueTriggered_Implementation(
	const FPaper2DPlusFrameCueContext& Context)
{
}

void UPaper2DPlusCueState::OnCueBegin_Implementation(
	const FPaper2DPlusFrameCueContext& Context)
{
}

void UPaper2DPlusCueState::OnCueUpdate_Implementation(
	const FPaper2DPlusFrameCueContext& Context)
{
}

void UPaper2DPlusCueState::OnCueEnd_Implementation(
	const FPaper2DPlusFrameCueContext& Context)
{
}

bool UPaper2DPlusCue::RemapFrameAnchors(const TArray<int32>& OldToNew, int32 NumNewFrames)
{
	if (OldToNew.IsValidIndex(TriggerFrame))
	{
		const int32 NewIndex = OldToNew[TriggerFrame];
		if (NewIndex == INDEX_NONE)
		{
			return false;
		}
		TriggerFrame = NewIndex;
		return true;
	}

	if (NumNewFrames > 0)
	{
		TriggerFrame = FMath::Clamp(TriggerFrame, 0, NumNewFrames - 1);
	}
	return true;
}

bool UPaper2DPlusCueState::RemapFrameAnchors(const TArray<int32>& OldToNew, int32 NumNewFrames)
{
	const int32 OldStart = StartFrame;
	const int32 OldEnd = OldStart + GetCueFrameCount() - 1;

	if (OldToNew.IsValidIndex(OldStart))
	{
		const int32 NewStart = OldToNew[OldStart];
		if (NewStart == INDEX_NONE)
		{
			return false;
		}

		int32 NewMin = NewStart;
		int32 NewMax = NewStart;
		const int32 ClampedEnd = FMath::Min(OldEnd, OldToNew.Num() - 1);
		for (int32 OldFrame = OldStart; OldFrame <= ClampedEnd; ++OldFrame)
		{
			const int32 Mapped = OldToNew[OldFrame];
			if (Mapped != INDEX_NONE)
			{
				NewMin = FMath::Min(NewMin, Mapped);
				NewMax = FMath::Max(NewMax, Mapped);
			}
		}
		StartFrame = NewMin;
		FrameCount = FMath::Max(1, NewMax - NewMin + 1);
	}

	if (NumNewFrames > 0)
	{
		StartFrame = FMath::Clamp(StartFrame, 0, NumNewFrames - 1);
		FrameCount = FMath::Clamp(FrameCount, 1, NumNewFrames - StartFrame);
	}
	return true;
}

#if WITH_EDITOR
FString FPaper2DPlusFrameCueTrackLayout::NormalizeTrackName(const FString& InName)
{
	FString Result = InName;
	Result.TrimStartAndEndInline();
	return Result;
}

FString FPaper2DPlusFrameCueTrackLayout::SuggestTrackName() const
{
	for (int32 Number = 2; Number < MAX_int32; ++Number)
	{
		const FString Candidate = FString::Printf(TEXT("Track %d"), Number);
		if (IsTrackNameAvailable(Candidate))
		{
			return Candidate;
		}
	}
	return TEXT("Track");
}

bool FPaper2DPlusFrameCueTrackLayout::IsTrackIdValid(const FGuid& TrackId) const
{
	if (!TrackId.IsValid())
	{
		return false;
	}
	int32 MatchCount = 0;
	for (const FPaper2DPlusFrameCueTrackDefinition& Track : OptionalTracks)
	{
		if (Track.TrackId == TrackId)
		{
			++MatchCount;
		}
	}
	return MatchCount == 1;
}

bool FPaper2DPlusFrameCueTrackLayout::IsTrackNameAvailable(
	const FString& DisplayName,
	const FGuid& IgnoreTrackId) const
{
	const FString Normalized = NormalizeTrackName(DisplayName);
	if (Normalized.IsEmpty())
	{
		return false;
	}
	for (const FPaper2DPlusFrameCueTrackDefinition& Track : OptionalTracks)
	{
		if (Track.TrackId != IgnoreTrackId
			&& NormalizeTrackName(Track.DisplayName).Equals(Normalized, ESearchCase::IgnoreCase))
		{
			return false;
		}
	}
	return true;
}

bool FPaper2DPlusFrameCueTrackLayout::AddTrack(
	const FString& DisplayName,
	FGuid RequestedTrackId,
	FGuid& OutTrackId)
{
	OutTrackId.Invalidate();
	const FString Normalized = NormalizeTrackName(DisplayName);
	if (!IsTrackNameAvailable(Normalized))
	{
		return false;
	}
	if (!RequestedTrackId.IsValid())
	{
		RequestedTrackId = FGuid::NewGuid();
	}
	if (IsTrackIdValid(RequestedTrackId)
		|| OptionalTracks.ContainsByPredicate(
			[&RequestedTrackId](const FPaper2DPlusFrameCueTrackDefinition& Track)
			{
				return Track.TrackId == RequestedTrackId;
			}))
	{
		return false;
	}

	FPaper2DPlusFrameCueTrackDefinition& Track = OptionalTracks.AddDefaulted_GetRef();
	Track.TrackId = RequestedTrackId;
	Track.DisplayName = Normalized;
	OutTrackId = RequestedTrackId;
	return true;
}

bool FPaper2DPlusFrameCueTrackLayout::RenameTrack(
	const FGuid& TrackId,
	const FString& DisplayName)
{
	if (!IsTrackIdValid(TrackId) || !IsTrackNameAvailable(DisplayName, TrackId))
	{
		return false;
	}
	FPaper2DPlusFrameCueTrackDefinition* Track = OptionalTracks.FindByPredicate(
		[&TrackId](const FPaper2DPlusFrameCueTrackDefinition& Candidate)
		{
			return Candidate.TrackId == TrackId;
		});
	if (!Track)
	{
		return false;
	}
	Track->DisplayName = NormalizeTrackName(DisplayName);
	return true;
}

bool FPaper2DPlusFrameCueTrackLayout::ReorderTrack(
	const FGuid& TrackId,
	int32 NewOptionalTrackIndex)
{
	if (!IsTrackIdValid(TrackId)
		|| !OptionalTracks.IsValidIndex(NewOptionalTrackIndex))
	{
		return false;
	}
	const int32 CurrentIndex = OptionalTracks.IndexOfByPredicate(
		[&TrackId](const FPaper2DPlusFrameCueTrackDefinition& Track)
		{
			return Track.TrackId == TrackId;
		});
	if (CurrentIndex == INDEX_NONE)
	{
		return false;
	}
	if (CurrentIndex == NewOptionalTrackIndex)
	{
		return true;
	}

	FPaper2DPlusFrameCueTrackDefinition Moving = MoveTemp(OptionalTracks[CurrentIndex]);
	OptionalTracks.RemoveAt(CurrentIndex);
	OptionalTracks.Insert(MoveTemp(Moving), NewOptionalTrackIndex);
	return true;
}

bool FPaper2DPlusFrameCueTrackLayout::RemoveTrackDefinition(const FGuid& TrackId)
{
	if (!IsTrackIdValid(TrackId))
	{
		return false;
	}
	const int32 TrackIndex = OptionalTracks.IndexOfByPredicate(
		[&TrackId](const FPaper2DPlusFrameCueTrackDefinition& Track)
		{
			return Track.TrackId == TrackId;
		});
	if (TrackIndex == INDEX_NONE)
	{
		return false;
	}
	OptionalTracks.RemoveAt(TrackIndex);
	return true;
}

FGuid FPaper2DPlusFrameCueTrackLayout::FindStoredTrackId(
	const UPaper2DPlusCueBase* Cue) const
{
	if (!Cue)
	{
		return FGuid();
	}
	const FGuid* TrackId = CueTrackIds.Find(const_cast<UPaper2DPlusCueBase*>(Cue));
	return TrackId ? *TrackId : FGuid();
}

FGuid FPaper2DPlusFrameCueTrackLayout::ResolveStoredTrackId(
	const UPaper2DPlusCueBase* Cue) const
{
	const FGuid TrackId = FindStoredTrackId(Cue);
	return IsTrackIdValid(TrackId) ? TrackId : FGuid();
}

bool FPaper2DPlusFrameCueTrackLayout::AssignCue(
	UPaper2DPlusCueBase* Cue,
	const FGuid& TrackId)
{
	if (!Cue)
	{
		return false;
	}
	if (!TrackId.IsValid())
	{
		CueTrackIds.Remove(Cue);
		return true;
	}
	if (!IsTrackIdValid(TrackId))
	{
		return false;
	}
	CueTrackIds.Add(Cue, TrackId);
	return true;
}

void FPaper2DPlusFrameCueTrackLayout::RemoveCue(UPaper2DPlusCueBase* Cue)
{
	if (Cue)
	{
		CueTrackIds.Remove(Cue);
	}
}

int32 FPaper2DPlusFrameCueTrackLayout::RemoveAssignmentsForTrack(const FGuid& TrackId)
{
	TArray<TObjectPtr<UPaper2DPlusCueBase>> KeysToRemove;
	for (const TPair<TObjectPtr<UPaper2DPlusCueBase>, FGuid>& Pair : CueTrackIds)
	{
		if (Pair.Value == TrackId)
		{
			KeysToRemove.Add(Pair.Key);
		}
	}
	for (UPaper2DPlusCueBase* Cue : KeysToRemove)
	{
		CueTrackIds.Remove(Cue);
	}
	return KeysToRemove.Num();
}

bool FPaper2DPlusFrameCueTrackLayout::RemapCueReferences(
	const FPaper2DPlusFrameCueReplacementMap& Replacements)
{
	if (Replacements.IsEmpty() || CueTrackIds.IsEmpty())
	{
		return false;
	}

	TMap<TObjectPtr<UPaper2DPlusCueBase>, FGuid> Remapped;
	TSet<TObjectPtr<UPaper2DPlusCueBase>> Conflicts;
	bool bChanged = false;
	for (const TPair<TObjectPtr<UPaper2DPlusCueBase>, FGuid>& Pair : CueTrackIds)
	{
		UPaper2DPlusCueBase* Destination = Pair.Key.Get();
		if (UPaper2DPlusCueBase* const* Replacement = Replacements.Find(Pair.Key.Get()))
		{
			Destination = *Replacement;
			bChanged = true;
		}
		if (!Destination)
		{
			continue;
		}
		if (const FGuid* Existing = Remapped.Find(Destination); Existing && *Existing != Pair.Value)
		{
			Conflicts.Add(Destination);
			continue;
		}
		Remapped.Add(Destination, Pair.Value);
	}
	for (UPaper2DPlusCueBase* Conflict : Conflicts)
	{
		Remapped.Remove(Conflict);
	}
	bChanged |= Remapped.Num() != CueTrackIds.Num();
	CueTrackIds = MoveTemp(Remapped);
	return bChanged;
}

void FPaper2DPlusFrameCueTrackLayout::RetainCueAssignments(
	const TSet<const UPaper2DPlusCueBase*>& ValidCues)
{
	TArray<TObjectPtr<UPaper2DPlusCueBase>> KeysToRemove;
	for (const TPair<TObjectPtr<UPaper2DPlusCueBase>, FGuid>& Pair : CueTrackIds)
	{
		if (!Pair.Key || !ValidCues.Contains(Pair.Key.Get()))
		{
			KeysToRemove.Add(Pair.Key);
		}
	}
	for (UPaper2DPlusCueBase* Cue : KeysToRemove)
	{
		CueTrackIds.Remove(Cue);
	}
}
#endif // WITH_EDITOR
