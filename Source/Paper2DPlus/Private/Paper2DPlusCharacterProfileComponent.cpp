// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusCharacterProfileComponent.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "FrameEvents/Paper2DPlusFrameEventBase.h"
#include "FrameEvents/Paper2DPlusFrameEventState.h"
#include "Paper2DPlusDebugComponent.h"
#include "Paper2DPlusFlipbookComponent.h"
#include "Paper2DPlusModule.h"
#include "PaperFlipbookComponent.h"
#include "PaperFlipbook.h"
#include "Engine/World.h"

/** UPaper2DPlusCharacterProfileComponent — Frame event dispatch, root motion application, and animation lifecycle management for actors. */

// Slow-poll interval for flipbook change detection when the current
// animation has no active features. 20Hz is sufficient — worst case 50ms
// before root motion/effects kick in, imperceptible since frame 0 is usually (0,0).
static constexpr float SlowPollInterval = 0.05f;

UPaper2DPlusCharacterProfileComponent::UPaper2DPlusCharacterProfileComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UPaper2DPlusCharacterProfileComponent::SetCharacterProfile(UPaper2DPlusCharacterProfileAsset* NewCharacterProfile)
{
	if (CharacterProfile == NewCharacterProfile) return;

	CharacterProfile = NewCharacterProfile;

	// Force the new asset to rebuild its flipbook lookup cache so
	// FindByFlipbookPtr resolves soft references immediately
	if (CharacterProfile)
	{
		CharacterProfile->InvalidateFlipbookLookupCache();
	}

	// Re-resolve caches and dispatch frame-0 for the new profile. This replaces
	// the previous ResetRootMotionTracking() call, which was over-scoped and
	// would leave the shared cache stale-pointing at the old
	// profile's data. HandleFlipbookChanged routes through OnFlipbookChanged
	// (warms caches + seeds baseline) and then dispatches frame-0 effects +
	// root motion for the new profile in one pass.
	if (UPaperFlipbookComponent* FBComp = GetResolvedFlipbookComponent())
	{
		HandleFlipbookChanged(FBComp->GetFlipbook());
	}
	else
	{
		// No flipbook component yet — BeginPlay hasn't run. Narrow reset;
		// cache warming will happen on the first HandleFlipbookChanged.
		ResetRootMotionTracking();
	}

	// Sync the debug component's cached copy if present
	if (AActor* Owner = GetOwner())
	{
		if (UPaper2DPlusDebugComponent* DebugComp = Owner->FindComponentByClass<UPaper2DPlusDebugComponent>())
		{
			DebugComp->CharacterProfile = CharacterProfile;
		}

		UE_LOG(LogPaper2DPlus, Log,
			TEXT("SetCharacterProfile: '%s' on '%s' → %s"),
			*GetName(), *Owner->GetName(),
			CharacterProfile ? *CharacterProfile->GetName() : TEXT("null"));
	}
}

void UPaper2DPlusCharacterProfileComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!FlipbookComponent)
	{
		AActor* Owner = GetOwner();
		if (Owner)
		{
			FlipbookComponent = Owner->FindComponentByClass<UPaperFlipbookComponent>();
			if (FlipbookComponent)
			{
				UE_LOG(LogPaper2DPlus, Verbose, TEXT("CharacterProfileComponent: Auto-found FlipbookComponent on %s"), *Owner->GetName());
			}
		}
	}

	// Bind to event-driven flipbook + frame detection if the component is our subclass
	if (UPaper2DPlusFlipbookComponent* P2PFBComp = Cast<UPaper2DPlusFlipbookComponent>(FlipbookComponent.Get()))
	{
		P2PFBComp->OnFlipbookChanged.AddDynamic(this, &UPaper2DPlusCharacterProfileComponent::HandleFlipbookChanged);
		P2PFBComp->OnFrameChanged.AddDynamic(this, &UPaper2DPlusCharacterProfileComponent::HandleFrameChanged);
		bEventDrivenFlipbookDetection = true;
	}

	// Always warm caches from the current flipbook so combat data, frame events,
	// and motion data are available immediately — not just when root motion is on.
	if (UPaperFlipbook* CurrentFB = FlipbookComponent ? FlipbookComponent->GetFlipbook() : nullptr)
	{
		HandleFlipbookChanged(CurrentFB);
	}
	if (bAutoApplyRootMotion)
	{
		UpdateTickState();
	}
}

void UPaper2DPlusCharacterProfileComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bEventDrivenFlipbookDetection)
	{
		if (UPaper2DPlusFlipbookComponent* P2PFBComp = Cast<UPaper2DPlusFlipbookComponent>(FlipbookComponent.Get()))
		{
			P2PFBComp->OnFlipbookChanged.RemoveDynamic(this, &UPaper2DPlusCharacterProfileComponent::HandleFlipbookChanged);
			P2PFBComp->OnFrameChanged.RemoveDynamic(this, &UPaper2DPlusCharacterProfileComponent::HandleFrameChanged);
		}
	}
	Super::EndPlay(EndPlayReason);
}

UPaperFlipbookComponent* UPaper2DPlusCharacterProfileComponent::GetResolvedFlipbookComponent() const
{
	if (FlipbookComponent)
	{
		return FlipbookComponent;
	}

	// Fallback: try to find at runtime if BeginPlay hasn't run yet or component was cleared
	AActor* Owner = GetOwner();
	if (Owner)
	{
		return Owner->FindComponentByClass<UPaperFlipbookComponent>();
	}
	return nullptr;
}

void UPaper2DPlusCharacterProfileComponent::SetAutoApplyRootMotion(bool bEnable)
{
	bAutoApplyRootMotion = bEnable;

	if (!bEnable)
	{
		ResetRootMotionTracking();
	}
	UpdateTickState();
}

void UPaper2DPlusCharacterProfileComponent::ResetRootMotionTracking()
{
	// Resets ONLY the mutable root-motion tracking state. Shared caches
	// (CachedCombatData, CachedMotionData, CachedFrameEventData, PreviousFlipbook)
	// are owned by OnFlipbookChanged and must NOT be touched here.
	PreviousFrameIndex = INDEX_NONE;

	// Re-seed baseline from the current frame's root-motion sample so that the
	// next HandleFrameChanged computes a correct delta instead of treating the
	// full authored position as movement (teleport).
	if (CachedMotionData)
	{
		UPaperFlipbookComponent* FBComp = GetResolvedFlipbookComponent();
		if (FBComp && FBComp->GetFlipbook())
		{
			const int32 CurrentFrame = FBComp->GetFlipbook()->GetKeyFrameIndexAtTime(FBComp->GetPlaybackPosition());
			if (CachedMotionData->RootMotion.IsValidIndex(CurrentFrame))
			{
				LastAppliedRootMotionPos = CachedMotionData->RootMotion[CurrentFrame].Position;
				return;
			}
		}
	}
	LastAppliedRootMotionPos = FVector2D::ZeroVector;
}

void UPaper2DPlusCharacterProfileComponent::OnFlipbookChanged(UPaperFlipbook* NewFlipbook)
{
	PreviousFlipbook = NewFlipbook;

	// Resolve and cache the flipbook data + feature flags
	CachedCombatData = nullptr;
	CachedMotionData = nullptr;
	CachedFrameEventData = nullptr;

	if (CharacterProfile && NewFlipbook)
	{
		if (const FFlipbookProfileEntry* Entry = CharacterProfile->FindByFlipbookPtr(NewFlipbook))
		{
			Entry->GetCacheView(CachedCombatData, CachedMotionData, CachedFrameEventData);
		}
	}

	// Seed the root-motion baseline from the trajectory's reference frame
	// (frame 0). Frame 0 represents "where the character is" at the start of
	// the animation — it establishes the baseline, not a movement delta.
	// Seeding from RootMotion[0] makes the subsequent frame-0 dispatch
	// compute PixelDelta = RootMotion[0] - RootMotion[0] = 0, avoiding a
	// teleport when the authored frame-0 position is non-zero. Walk-up
	// trajectories with RootMotion[0] = (0,0) remain correct because the
	// seed is also (0,0).
	LastAppliedRootMotionPos = (CachedMotionData && CachedMotionData->RootMotion.IsValidIndex(0))
		? CachedMotionData->RootMotion[0].Position
		: FVector2D::ZeroVector;

	UpdateTickState();
}

void UPaper2DPlusCharacterProfileComponent::HandleFlipbookChanged(UPaperFlipbook* NewFlipbook)
{
	// Re-entry guard: a BP OnFrameEventEnd handler that synchronously calls
	// SetFlipbook would re-enter this routine and corrupt the ActiveRangedEvents
	// iteration below (TSet.Empty() while the outer loop is walking it).
	if (bHandlingFlipbookChange)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("HandleFlipbookChanged re-entered during flipbook change; nested call ignored."));
		return;
	}
	TGuardValue<bool> ReentryGuard(bHandlingFlipbookChange, true);

	// End all active ranged events from the previous flipbook before warming new cache.
	// Swap-out to a local array so nested handler mutations can't invalidate iteration.
	if (ActiveRangedEvents.Num() > 0)
	{
		TArray<TObjectPtr<UPaper2DPlusFrameEventBase>> ToEnd = ActiveRangedEvents.Array();
		ActiveRangedEvents.Empty();

		const FPaper2DPlusFrameEventContext EndContext = {
			GetOwner(), this, INDEX_NONE, PreviousFrameIndex, false
		};
		for (UPaper2DPlusFrameEventBase* Active : ToEnd)
		{
			if (IsValid(Active))
			{
				Active->DispatchForceEnd(EndContext);
			}
		}
	}

	OnFlipbookChanged(NewFlipbook);

	// SetFlipbook resets playback position to 0. Compute the target frame but
	// do NOT set PreviousFrameIndex — HandleFrameChanged needs to see the
	// transition from INDEX_NONE (no previous frame) to frame 0 so that
	// frame events at frame 0 fire correctly.
	const int32 Frame0 = NewFlipbook ? NewFlipbook->GetKeyFrameIndexAtTime(0.0f) : INDEX_NONE;
	if (Frame0 == INDEX_NONE) return;

	// Single source of truth: funnel through HandleFrameChanged for frame-0 dispatch
	// (root motion, effects, AND frame events all go through one path).
	PreviousFrameIndex = INDEX_NONE;
	HandleFrameChanged(Frame0);
}

/*
 * Frame event dispatch — called every time the flipbook advances to a new frame.
 *
 * Two event types:
 *   One-shot (UPaper2DPlusFrameEvent): fires once when CurrentFrame == TriggerFrame.
 *   Ranged (UPaper2DPlusFrameEventState): fires Begin on first frame in range,
 *     Tick every frame while in range, End when leaving range. Tracked via
 *     ActiveRangedEvents set — events are added on Begin, removed on End.
 *
 * Root motion: computes delta from authored position curve (RootMotion[Frame].Position).
 *   Delta = CurrentPos - LastAppliedPos, scaled by flipbook component world scale.
 *   Applied via AddWorldOffset on the owning actor.
 *
 * Re-entry guard: bDispatchingFrameEvents prevents flipbook changes during dispatch
 *   from causing recursive HandleFrameChanged calls.
 */
void UPaper2DPlusCharacterProfileComponent::HandleFrameChanged(int32 NewFrame)
{
	if (NewFrame == INDEX_NONE) return;

	// Loop wrap = forward-playback frame index went backward.
	// Project does not use reverse playback (verified absent in plugin source);
	// if Reverse() / SetPlayRate(-1) is added later, revisit this heuristic.
	const bool bLoopWrap = (PreviousFrameIndex != INDEX_NONE && NewFrame < PreviousFrameIndex);

	if (bAutoApplyRootMotion && HasCachedRootMotion())
	{
		ApplyRootMotionForFrame(NewFrame, bLoopWrap);
	}

	// ─── Frame event dispatch ────────────────────────────────────────
	if (CachedFrameEventData && CachedFrameEventData->FrameEvents.Num() > 0)
	{
		// Re-entry guard: a BP handler might synchronously call SetFlipbook,
		// which fires HandleFlipbookChanged -> HandleFrameChanged(0). If the
		// outer call is still iterating, the nested call is dropped silently
		// and PreviousFrameIndex is left alone — overwriting it with NewFrame
		// (from a different flipbook's numbering) breaks the outer call's
		// loop-wrap detection when control returns.
		if (bDispatchingFrameEvents)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("HandleFrameChanged re-entered during frame event dispatch; nested call ignored."));
			return;
		}
		TGuardValue<bool> ReentryGuard(bDispatchingFrameEvents, true);

		// Snapshot iteration protects against handler-induced mutation
		TArray<TObjectPtr<UPaper2DPlusFrameEventBase>> Snapshot = CachedFrameEventData->FrameEvents;

		const FPaper2DPlusFrameEventContext Context = {
			GetOwner(), this, NewFrame, PreviousFrameIndex, bLoopWrap
		};

		// ActiveRangedEvents persists across calls — each ranged event manages its
		// own Add/Remove inside DispatchFrame. Track which events were processed
		// to detect stale entries (events removed from the array while still active).
		TSet<TObjectPtr<UPaper2DPlusFrameEventBase>> ProcessedThisFrame;

		for (UPaper2DPlusFrameEventBase* Event : Snapshot)
		{
			if (!Event) continue;
			const bool bFired = Event->DispatchFrame(Context, ActiveRangedEvents);
			ProcessedThisFrame.Add(Event);
			// Broadcast only when the event actually fired — otherwise BP listeners
			// get spurious every-frame signals for events that happen to be in the
			// snapshot but aren't triggering (one-shot on non-trigger frame,
			// ranged outside range, etc.). See PR #98 review finding #1.
			if (bFired)
			{
				OnFrameEventFired.Broadcast(Event, NewFrame, Event->DebugName);
			}
		}

		// Anything in ActiveRangedEvents that wasn't in the snapshot = stale
		TArray<TObjectPtr<UPaper2DPlusFrameEventBase>> StaleEvents;
		for (const TObjectPtr<UPaper2DPlusFrameEventBase>& Active : ActiveRangedEvents)
		{
			if (!ProcessedThisFrame.Contains(Active))
			{
				StaleEvents.Add(Active);
			}
		}
		for (UPaper2DPlusFrameEventBase* Stale : StaleEvents)
		{
			if (IsValid(Stale))
			{
				Stale->DispatchForceEnd(Context);
			}
			ActiveRangedEvents.Remove(Stale);
		}
	}

	PreviousFrameIndex = NewFrame;
}

FVector UPaper2DPlusCharacterProfileComponent::ComputeRootMotionWorldDelta(const FVector2D& PixelDelta) const
{
	// Pure conversion from pixel-space trajectory delta to world-space offset.
	// Shared between ApplyRootMotionForFrame (auto path) and GetRootMotionDelta
	// (manual BP query) so the scale + facing-flip math lives in exactly one
	// place. Const — does not advance the baseline.
	if (PixelDelta.IsNearlyZero()) return FVector::ZeroVector;

	UPaperFlipbookComponent* FBComp = GetResolvedFlipbookComponent();
	if (!FBComp) return FVector::ZeroVector;

	const FVector CompScale = FBComp->GetComponentScale();
	float WorldX = PixelDelta.X * FMath::Abs(CompScale.X);
	if (IsFacingLeft()) WorldX = -WorldX;
	const float WorldZ = PixelDelta.Y * FMath::Abs(CompScale.Z);

	return FVector(WorldX, 0.0f, WorldZ);
}

void UPaper2DPlusCharacterProfileComponent::ApplyRootMotionForFrame(int32 NewFrame, bool bLoopWrap)
{
	if (bLoopWrap)
	{
		// Seed the baseline to the wrap-to frame's position so the next
		// non-wrap frame computes delta against the correct reference.
		// Resetting to ZeroVector would teleport by RootMotion[wrap-to]
		// on the first post-wrap non-zero frame (Bug 3 regression).
		LastAppliedRootMotionPos = (CachedMotionData && CachedMotionData->RootMotion.IsValidIndex(NewFrame))
			? CachedMotionData->RootMotion[NewFrame].Position
			: FVector2D::ZeroVector;
		return;
	}

	if (!CachedMotionData || !CachedMotionData->RootMotion.IsValidIndex(NewFrame)) return;

	const FVector2D CurrentPos = CachedMotionData->RootMotion[NewFrame].Position;
	if (CurrentPos.IsNearlyZero()) return;   // sparse: (0,0) frames are authoring-blank

	const FVector2D PixelDelta = CurrentPos - LastAppliedRootMotionPos;
	LastAppliedRootMotionPos = CurrentPos;

	const FVector WorldDelta = ComputeRootMotionWorldDelta(PixelDelta);
	if (WorldDelta.IsNearlyZero()) return;

	if (AActor* Owner = GetOwner())
	{
		// World guard: production actors always have a world, so this costs
		// nothing at runtime — but it lets worldless unit tests exercise the
		// baseline-advance logic without tripping engine ensures inside
		// AddActorWorldOffset.
		if (Owner->GetWorld())
		{
			Owner->AddActorWorldOffset(WorldDelta);
		}
	}
}

bool UPaper2DPlusCharacterProfileComponent::NeedsTick() const
{
	if (bAutoApplyRootMotion && HasCachedRootMotion()) return true;
	return false;
}

void UPaper2DPlusCharacterProfileComponent::UpdateTickState()
{
	// Event-driven path: zero ticking. UPaper2DPlusFlipbookComponent fires
	// OnFlipbookChanged + OnFrameChanged delegates that drive HandleFrameChanged
	// directly, and effect lifetimes are timer-managed.
	if (bEventDrivenFlipbookDetection)
	{
		PrimaryComponentTick.SetTickFunctionEnable(false);
		return;
	}

	// Slow-poll fallback (stock UPaperFlipbookComponent users):
	// 20Hz when no features active, full speed when features need detection.
	if (!bAutoApplyRootMotion)
	{
		PrimaryComponentTick.SetTickFunctionEnable(false);
		return;
	}

	PrimaryComponentTick.TickInterval = NeedsTick() ? 0.0f : SlowPollInterval;
	PrimaryComponentTick.SetTickFunctionEnable(true);
}

void UPaper2DPlusCharacterProfileComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Slow-poll fallback only — event-driven path has tick disabled.
	// UpdateTickState() guards this, but belt-and-braces for the rare case where
	// tick is enabled before BeginPlay binds the delegates.
	if (bEventDrivenFlipbookDetection) return;

	UPaperFlipbookComponent* FBComp = GetResolvedFlipbookComponent();
	if (!FBComp) return;

	UPaperFlipbook* CurrentFlipbook = FBComp->GetFlipbook();

	// Detect flipbook change → funnel through the unified handler
	// (which warms caches, dispatches frame-0 effects + root motion).
	if (CurrentFlipbook != PreviousFlipbook.Get())
	{
		HandleFlipbookChanged(CurrentFlipbook);
		return;
	}

	if (!CurrentFlipbook) return;

	// Detect frame change → funnel through the unified handler
	// (which detects loop wrap and dispatches root motion + effects).
	const int32 CurrentFrame = CurrentFlipbook->GetKeyFrameIndexAtTime(FBComp->GetPlaybackPosition());
	if (CurrentFrame != PreviousFrameIndex && CurrentFrame != INDEX_NONE)
	{
		HandleFrameChanged(CurrentFrame);
	}
}

FVector UPaper2DPlusCharacterProfileComponent::GetRootMotionDelta() const
{
	// Const query: returns the world-space delta that would be applied this
	// frame if auto-apply were enabled. Does NOT advance the baseline
	// (LastAppliedRootMotionPos). Repeated calls within a single frame
	// return the same delta. See ConsumeRootMotionDelta for the mutating
	// variant used by manual-drive movement components.
	if (!CachedMotionData || !HasCachedRootMotion()) return FVector::ZeroVector;

	UPaperFlipbookComponent* FBComp = GetResolvedFlipbookComponent();
	if (!FBComp || !FBComp->GetFlipbook()) return FVector::ZeroVector;

	const int32 CurrentFrame = FBComp->GetFlipbook()->GetKeyFrameIndexAtTime(FBComp->GetPlaybackPosition());
	if (!CachedMotionData->RootMotion.IsValidIndex(CurrentFrame)) return FVector::ZeroVector;

	const FVector2D CurrentPos = CachedMotionData->RootMotion[CurrentFrame].Position;
	if (CurrentPos.IsNearlyZero()) return FVector::ZeroVector;

	return ComputeRootMotionWorldDelta(CurrentPos - LastAppliedRootMotionPos);
}

FVector UPaper2DPlusCharacterProfileComponent::ConsumeRootMotionDelta()
{
	// Mutating variant: returns the current delta AND advances the baseline
	// so the next call within the same frame returns zero. Designed for
	// manual-drive movement components that integrate root motion into their
	// own velocity model (bAutoApplyRootMotion=false).
	if (!CachedMotionData || !HasCachedRootMotion()) return FVector::ZeroVector;

	UPaperFlipbookComponent* FBComp = GetResolvedFlipbookComponent();
	if (!FBComp || !FBComp->GetFlipbook()) return FVector::ZeroVector;

	const int32 CurrentFrame = FBComp->GetFlipbook()->GetKeyFrameIndexAtTime(FBComp->GetPlaybackPosition());
	if (!CachedMotionData->RootMotion.IsValidIndex(CurrentFrame)) return FVector::ZeroVector;

	const FVector2D CurrentPos = CachedMotionData->RootMotion[CurrentFrame].Position;
	if (CurrentPos.IsNearlyZero()) return FVector::ZeroVector;

	const FVector2D PixelDelta = CurrentPos - LastAppliedRootMotionPos;
	LastAppliedRootMotionPos = CurrentPos;
	return ComputeRootMotionWorldDelta(PixelDelta);
}

bool UPaper2DPlusCharacterProfileComponent::IsFacingLeft() const
{
	UPaperFlipbookComponent* FBComp = GetResolvedFlipbookComponent();
	if (!FBComp) return false;

	const FVector CompScale = FBComp->GetComponentScale();
	const float Yaw = FMath::Abs(FBComp->GetComponentRotation().Yaw);

	// Same logic as hitbox resolution: Yaw > 90 or negative X scale means facing left
	return (Yaw > 90.0f && Yaw < 270.0f) || CompScale.X < 0.0f;
}


