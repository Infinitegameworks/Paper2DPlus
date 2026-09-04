// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusCharacterProfileComponent.h"
#include "Paper2DPlusAppearanceResolver.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "Paper2DPlusLayerCombat.h"
// Frame Cue dispatch and warming are class-agnostic: this file names no built-in cue class, so it
// includes the base declaration only. Reintroducing a built-in include here is the smell that a
// class check has crept back into the seam.
#include "FrameCues/Paper2DPlusFrameCue.h"
#include "FrameCues/Paper2DPlusFrameCueDispatch.h"
#include "FrameCues/Paper2DPlusFrameCuePlaybackObserver.h"
#include "Paper2DPlusDebugComponent.h"
#include "Paper2DPlusFlipbookComponent.h"
#include "Paper2DPlusFrameGeometry.h"
#include "Paper2DPlusHitboxSubsystem.h"
#include "Paper2DPlusModule.h"
#include "Paper2DPlusNetGating.h"
#include "Paper2DPlusSettings.h"
#include "PaperFlipbookComponent.h"
#include "PaperFlipbook.h"
#include "PaperSprite.h"
#include "Engine/AssetManager.h"
#include "Engine/Engine.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "Misc/App.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/MovementComponent.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ObjectKey.h"

/** UPaper2DPlusCharacterProfileComponent — Frame Cue dispatch, root motion, and animation lifecycle. */

void UPaper2DPlusFrameCueStockPlaybackObserver::HandleFinishedPlaying()
{
	if (UPaper2DPlusCharacterProfileComponent* ProfileComponent = Owner.Get())
	{
		ProfileComponent->HandleBoundStockPlaybackFinished(Source.Get(), BindingEpoch);
	}
}

// Slow-poll interval for flipbook change detection on an actor that has NOTHING to dispatch from
// any animation. 20Hz costs a 50ms delay before root motion kicks in, which is imperceptible since
// frame 0 is usually (0,0) — but that latency argument does NOT transfer to Frame Cues, where an
// animation the sample never sees loses its anchors outright. UpdateTickState therefore reserves
// this rate for cue-free profiles; see the rate decision there.
static constexpr float SlowPollInterval = 0.05f;

UPaper2DPlusCharacterProfileComponent::UPaper2DPlusCharacterProfileComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;

	// TASK-57 U2: replication is strictly opt-in per component — BeginPlay turns it on when
	// bEnableReplication is set. Explicit even though the engine default is off (the opt-in contract).
	SetIsReplicatedByDefault(false);
}

void UPaper2DPlusCharacterProfileComponent::SetCharacterProfile(UPaper2DPlusCharacterProfileAsset* NewCharacterProfile)
{
	// Non-authority gate (TASK-57 KTD-22): the profile is the highest-blast-radius mutator and
	// single-player-era BP calls it on ALL machines — on a networked non-authority context it is
	// warn-once rejected. The OnRep_CharacterProfile funnel bypasses via bApplyingReplicatedState
	// (the replicated apply MUST route through this very funnel — single-cache-writer discipline).
	// Standalone falls through untouched — the single-player body below is verbatim.
	const EPaper2DPlusNetContext NetCtx = ResolveNetContext();
	if ((NetCtx == EPaper2DPlusNetContext::AutonomousProxy || NetCtx == EPaper2DPlusNetContext::SimulatedProxy)
		&& !bApplyingReplicatedState)
	{
		if (!bWarnedSetProfileOnProxy)
		{
			bWarnedSetProfileOnProxy = true;
			UE_LOG(LogPaper2DPlus, Warning,
				TEXT("SetCharacterProfile called on a non-authority networked context on '%s' — ignored. The profile is server-authoritative when bEnableReplication is on: set it on the server and it replicates down (TASK-57)."),
				*GetName());
		}
		return;
	}

	if (CharacterProfile == NewCharacterProfile) return;

	// The Profile is part of every Cue invocation snapshot. Claim the outgoing generation BEFORE
	// replacing that identity so a Profile A→B swap (including the same flipbook in both assets)
	// cannot label A's paired End with B. During Cue delivery the commit itself waits behind the
	// first-wins terminal transaction.
	if (FrameCuePlaybackGeneration > 0)
	{
		ClaimFrameCueTerminal(
			EPaper2DPlusFrameCueEndReason::AnimationChanged,
			RuntimeFrameCueContext.PlaybackComponent,
			FrameCuePlaybackGeneration);
		if (bDispatchingFrameCues || bHandlingFlipbookChange || bHasPendingFrameCueTerminal)
		{
			PendingCharacterProfile = NewCharacterProfile;
			bHasPendingCharacterProfile = true;
			return;
		}
	}

	CharacterProfile = NewCharacterProfile;
	WarmedFrameCueEffectFlipbooks.Reset();
	// The detection rate is chosen from whether the profile carries cues at all, so a new profile
	// invalidates that answer before anything can read a stale one. The composed-Layer latch is an
	// OBSERVATION about the profile being left behind, so it is dropped on the same terms: keeping it
	// would watch a component re-pointed at a cue-free profile every frame for the rest of its life.
	// The direction is safe either way (over-tick, never under-tick) — the reset is simply free.
	bCueScanValid = false;
	bObservedComposedLayerCues = false;

	// Profile generation (TASK-57 KTD-6) — authority only (clients adopt the replicated RepProfileSeq
	// in OnRep_CharacterProfile instead). Bumped BEFORE the HandleFlipbookChanged funnel below so the
	// publish this swap triggers stamps the NEW generation: a tail-side bump would pair the swap
	// publish with the OLD seq and wedge the client's stash until the next move change.
	if (NetCtx == EPaper2DPlusNetContext::Authority)
	{
		++ProfileChangeCounter;
		RepProfileSeq = ProfileChangeCounter;
	}

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
		if (FBComp->GetFlipbook())
		{
			HandleFlipbookChanged(FBComp->GetFlipbook());
		}
		else
		{
			// Profile set before flipbook is assigned (common when GameMode sets
			// profile at spawn, but PaperZD assigns flipbook later). Enable tick
			// so the slow-poll path can detect the flipbook when it appears —
			// through the ONE function that owns the rate, never a second copy of the policy.
			ResetRootMotionTracking();
			UpdateTickState();
		}
	}
	else
	{
		ResetRootMotionTracking();
	}

	// Late profile assignment goes through the SAME placement funnel as BeginPlay, after the frame-zero
	// dispatch above — a profile handed over at spawn time places identically to one on the archetype.
	ApplyProfileRelativeTransform();

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

namespace
{
	// Converts a world-space sprite-offset displacement into the flipbook component's PARENT space —
	// the exact delta AddRelativeLocation must receive to realize it. The applied-offset record is
	// kept in this space (not world space) because a facing flip that rotates or mirrors the
	// actor/capsule mirrors the applied delta in world space all by itself; a world-space record
	// went stale at that instant and every subsequent commit double-applied the correction, walking
	// the sprite 2x the authored offset further from the capsule on each left/right flick.
	FVector SpriteOffsetWorldToParentSpace(const USceneComponent& Component, const FVector& WorldOffset)
	{
		if (const USceneComponent* Parent = Component.GetAttachParent())
		{
			return Parent->GetComponentTransform().InverseTransformVector(WorldOffset);
		}
		return WorldOffset;
	}
}

FVector UPaper2DPlusCharacterProfileComponent::ComputeFrameSpriteOffset(int32 FrameIndex) const
{
	if (!CachedCombatData || !CachedCombatData->FrameExtractionInfo.IsValidIndex(FrameIndex))
	{
		return FVector::ZeroVector;
	}

	const FSpriteExtractionInfo& Info = CachedCombatData->FrameExtractionInfo[FrameIndex];
	const FIntPoint Combined = Info.SpriteOffset + Info.TrimOffset;
	if (Combined == FIntPoint::ZeroValue)
	{
		return FVector::ZeroVector;
	}

	UPaperFlipbookComponent* FBComp = GetResolvedFlipbookComponent();
	if (!FBComp)
	{
		return FVector::ZeroVector;
	}

	float PPU = 1.0f;
	if (UPaperFlipbook* FB = FBComp->GetFlipbook())
	{
		if (FB->GetNumKeyFrames() > 0)
		{
			if (UPaperSprite* Sprite = FB->GetKeyFrameChecked(FMath::Min(FrameIndex, FB->GetNumKeyFrames() - 1)).Sprite)
			{
				PPU = FMath::Max(Sprite->GetPixelsPerUnrealUnit(), 0.001f);
			}
		}
	}

	const FVector CompScale = FBComp->GetComponentScale();
	// X mirrors exactly like hitboxes / root motion (audit F4): take the magnitude from
	// |scale| and apply the single sign flip via IsFacingLeft(), so a yaw-flipped facing
	// (CompScale.X stays positive) mirrors the per-frame offset too — not only negative scale.
	// Byte-identical for no-flip (+scale) and negative-scale cases; fixes only the yaw case.
	float OffsetX = (Combined.X / PPU) * FMath::Abs(CompScale.X);
	if (IsFacingLeft())
	{
		OffsetX = -OffsetX;
	}
	return FVector(
		OffsetX,
		0.0f,
		(-Combined.Y / PPU) * CompScale.Z);
}

void UPaper2DPlusCharacterProfileComponent::CommitSpriteOffset(
	const FVector& NewOffset,
	EPaper2DPlusNetContext NetCtx)
{
	// Hot path: no authored offset and nothing applied — return before resolving the component,
	// preserving the pre-conversion cost of the common zero/zero frame change.
	if (NewOffset.IsZero() && LastAppliedSpriteOffsetLocal.IsZero())
	{
		return;
	}

	UPaperFlipbookComponent* FBComp = GetResolvedFlipbookComponent();
	if (!FBComp)
	{
		return;
	}

	// Compare and record in PARENT space (the delta actually added to the component's relative
	// location), never world space. A facing flip that rotates the actor/capsule mirrors the
	// applied delta in world space on its own — the freshly computed world offset then converts
	// to the SAME parent-space value already applied, so this correctly no-ops. A world-space
	// record went stale at that instant and the next commit double-applied the correction,
	// drifting the sprite 2x the offset further out on every left/right facing flick.
	const FVector NewLocalOffset = SpriteOffsetWorldToParentSpace(*FBComp, NewOffset);
	if (NewLocalOffset.Equals(LastAppliedSpriteOffsetLocal))
	{
		return;
	}

	// Cosmetic-vs-root gate (TASK-57 U8, audit F5): the per-frame sprite offset displaces
	// the FLIPBOOK COMPONENT, which is COSMETIC whenever that component is NOT the actor root —
	// it nudges only the sprite's local visual, never the actor's networked position, so a net
	// proxy should still apply it (movement replication owns the ACTOR's motion, not a child
	// component's local offset). The ONE exception is when the flipbook component IS the actor
	// root: then the offset displaces the whole actor and would fight movement replication, so
	// it must obey the authority root-motion gate exactly like ApplyRootMotionForFrame (KTD-16).
	// Hence: apply on authority/Standalone OR whenever the flipbook is not the root; skip only
	// the proxy-AND-root case. The AddRelativeLocation, MarkCachedWorldStateDirty(), and the
	// LastAppliedSpriteOffsetLocal record stay TOGETHER inside the branch — never split the apply
	// from the record (Codex F195c), or a later OnFlipbookChanged subtracts a phantom delta
	// that never moved the component.
	const bool bFlipbookIsRoot = GetOwner() && GetOwner()->GetRootComponent() == FBComp;
	if (Paper2DPlusNetGating::ShouldApplyRootMotion(NetCtx) || !bFlipbookIsRoot)
	{
		FBComp->AddRelativeLocation(NewLocalOffset - LastAppliedSpriteOffsetLocal);
		MarkCachedWorldStateDirty();
		LastAppliedSpriteOffsetLocal = NewLocalOffset;
	}
}

void UPaper2DPlusCharacterProfileComponent::RetireAppliedSpriteOffset()
{
	if (LastAppliedSpriteOffsetLocal.IsZero())
	{
		return;
	}

	if (UPaperFlipbookComponent* FBComp = GetResolvedFlipbookComponent())
	{
		// Subtract the literal parent-space delta that was applied — exact regardless of any facing
		// flip or parent-transform change since the apply (a world-space undo was not, leaving a
		// permanent residue on the relative location after every flip).
		FBComp->AddRelativeLocation(-LastAppliedSpriteOffsetLocal);
		MarkCachedWorldStateDirty();
	}
	LastAppliedSpriteOffsetLocal = FVector::ZeroVector;
}

void UPaper2DPlusCharacterProfileComponent::ReseedSpriteOffsetForCurrentFrame()
{
	if (!CachedCombatData || CachedCombatData->FrameExtractionInfo.Num() == 0)
	{
		return;
	}

	UPaperFlipbookComponent* FBComp = GetResolvedFlipbookComponent();
	if (!FBComp || !FBComp->GetFlipbook())
	{
		return;
	}

	// Key-frame index, never GetPlaybackPositionInFrames — see
	// docs/solutions/ue-paper2d-keyframe-vs-timeline-frame.md.
	const int32 CurrentFrame =
		FBComp->GetFlipbook()->GetKeyFrameIndexAtTime(FBComp->GetPlaybackPosition());
	if (CurrentFrame == INDEX_NONE)
	{
		return;
	}

	CommitSpriteOffset(ComputeFrameSpriteOffset(CurrentFrame), ResolveNetContext());
}

void UPaper2DPlusCharacterProfileComponent::SetApplyProfileRelativeTransform(bool bEnable)
{
	if (bApplyProfileRelativeTransform == bEnable)
	{
		return;
	}

	bApplyProfileRelativeTransform = bEnable;
	// Re-run the funnel so disabling RESTORES the authored transform rather than stranding the
	// profile's values on the component.
	ApplyProfileRelativeTransform();
}

void UPaper2DPlusCharacterProfileComponent::ApplyProfileRelativeTransform()
{
	UPaperFlipbookComponent* FBComp = GetResolvedFlipbookComponent();
	if (!FBComp)
	{
		// No target to place. Not an error — a profile can legitimately be set before the flipbook
		// component exists, and the funnel re-runs when one is assigned.
		return;
	}

	// A DIFFERENT flipbook component carries its own authored transform, so the previous capture does
	// not describe it. Drop the applied latch instead of restoring the old component's pose onto the
	// new one. (The component being replaced keeps whatever was last assigned to it — this component
	// no longer drives it, and reaching back into a target we have stopped tracking would be worse.)
	if (RelativeTransformCaptureTarget.Get() != FBComp)
	{
		RelativeTransformCaptureTarget = FBComp;
		bRelativeTransformApplied = false;
	}

	const bool bWantApply = bApplyProfileRelativeTransform && CharacterProfile != nullptr;

	if (!bWantApply && !bRelativeTransformApplied)
	{
		// DEFAULT-OFF PATH: there is nothing to apply and nothing to undo, so touch nothing at all —
		// no transform write, no sprite-offset retire/re-seed churn, and no world-state cache
		// invalidation. A project that never opts in must behave exactly as it did before this
		// existed, and this funnel runs on three lifecycle paths, so "harmless no-op" is not good
		// enough: it has to be an actual no-op.
		return;
	}

	// The retained sprite-offset record is the PARENT-SPACE delta applied to the component's relative
	// location, computed against the component's scale at seed time (see ComputeFrameSpriteOffset /
	// CommitSpriteOffset). Assigning a relative transform overwrites the component's location outright
	// (discarding the applied delta) and changes that scale, so the record must be retired BEFORE the
	// assignment and re-seeded after — otherwise the record claims an offset that either no longer
	// exists or was measured at the wrong scale, and the next frame change applies a wrong delta. On a
	// single-key-frame idle that next frame change never comes, so the sprite simply sits displaced.
	// Retiring first also means the authored capture below records the component's real authored pose
	// rather than one with a per-frame offset baked into it.
	RetireAppliedSpriteOffset();

	if (bWantApply)
	{
		if (!bRelativeTransformApplied)
		{
			AuthoredFlipbookRelativeTransform = FBComp->GetRelativeTransform();
		}

		// ABSOLUTE assignment, never a retained delta (KTD7): running this twice, or alongside a game
		// that applies the same profile values itself, lands on the same transform instead of stacking.
		FBComp->SetRelativeTransform(CharacterProfile->GetRelativeTransform());
		bRelativeTransformApplied = true;
	}
	else
	{
		FBComp->SetRelativeTransform(AuthoredFlipbookRelativeTransform);
		bRelativeTransformApplied = false;
	}
	MarkCachedWorldStateDirty();

	ReseedSpriteOffsetForCurrentFrame();
}

void UPaper2DPlusCharacterProfileComponent::BeginPlay()
{
	Super::BeginPlay();

	// ─── Networking opt-in (TASK-57 U2) ─────────────────────────────────
	// bEnableReplication is CONSUMED here: the snapshot is what ResolveNetContext honors for the
	// rest of play (a later flip is inert and logged once — see ResolveNetContext).
	bReplicationEnabledAtBeginPlay = bEnableReplication;
	if (bEnableReplication)
	{
		SetIsReplicated(true);
	}

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

	// One atomic binding seam owns both the sender-aware custom path and stock compatibility path.
	BindFrameCuePlaybackSource(FlipbookComponent);
	if (FlipbookComponent && !bEventDrivenFlipbookDetection)
	{
		// Attributable and per component, not a single process-wide line that names nobody: whoever
		// reads this needs to know WHICH actor is on the compatibility path. It stays at Log level on
		// purpose — the compatibility path is supported, and the loud warnings are reserved for the
		// two shapes where cues actually cannot be delivered (see the detection diagnostics below).
		UE_LOG(LogPaper2DPlus, Log,
			TEXT("Actor '%s': '%s' is a stock UPaperFlipbookComponent, so Frame Cue detection runs in poll compatibility mode. UPaper2DPlusFlipbookComponent pushes flipbook/frame changes instead and costs no tick."),
			*GetNameSafe(GetOwner()),
			*FlipbookComponent->GetName());
	}

	// Always warm caches from the current flipbook so combat data, Frame Cues,
	// and motion data are available immediately — not just when root motion is on.
	if (UPaperFlipbook* CurrentFB = FlipbookComponent ? FlipbookComponent->GetFlipbook() : nullptr)
	{
		HandleFlipbookChanged(CurrentFB);
	}
	UpdateTickState();

	// Profile-driven sprite placement (TASK-157 U1). Ordered deliberately AFTER the frame-zero dispatch
	// above: that dispatch seeds the per-frame sprite-offset record at the component's pre-apply scale,
	// and the funnel re-seeds it at the applied scale. Running before would leave the ordering dependent
	// on whether a flipbook happened to be assigned yet.
	ApplyProfileRelativeTransform();

	if (bRegisterWithHitboxSubsystem)
	{
		if (UWorld* World = GetWorld())
		{
			if (UPaper2DPlusHitboxSubsystem* HitboxSubsystem = World->GetSubsystem<UPaper2DPlusHitboxSubsystem>())
			{
				HitboxSubsystem->RegisterProfileComponent(this);
			}
		}
	}

	// ─── Networking configuration warning battery (TASK-57 U2) ─────────
	// Each warning is inherently once (BeginPlay runs once per component). Server-configuration
	// warnings emit on AUTHORITY only — clients warn only about their own local mismatches, so
	// server config never leaks into client logs.
	if (bEnableReplication)
	{
		const EPaper2DPlusNetContext NetCtx = ResolveNetContext();
		AActor* Owner = GetOwner();

		if (NetCtx == EPaper2DPlusNetContext::Authority && Owner)
		{
			if (!Owner->GetIsReplicated())
			{
				UE_LOG(LogPaper2DPlus, Warning,
					TEXT("bEnableReplication is set on '%s' but owner '%s' does not replicate (bReplicates=false) — no Paper2DPlus state will reach clients."),
					*GetName(), *Owner->GetName());
			}
			if (bAutoApplyRootMotion && !Owner->IsReplicatingMovement())
			{
				UE_LOG(LogPaper2DPlus, Warning,
					TEXT("bAutoApplyRootMotion with bEnableReplication on '%s', but owner '%s' is not replicating movement — proxies will see ZERO root motion. Enable movement replication on the owner."),
					*GetName(), *Owner->GetName());
			}
			if (bAutoApplyRootMotion && Owner->FindComponentByClass<UCharacterMovementComponent>())
			{
				UE_LOG(LogPaper2DPlus, Warning,
					TEXT("bAutoApplyRootMotion with bEnableReplication on '%s', and owner '%s' has a CharacterMovementComponent — auto root motion fights the CMC's own movement replication. Set bAutoApplyRootMotion=false and integrate root motion into the movement component instead (TASK-57 authority contract)."),
					*GetName(), *Owner->GetName());
			}
		}

		if (NetCtx != EPaper2DPlusNetContext::Standalone)
		{
			UPaperFlipbookComponent* FBComp = GetResolvedFlipbookComponent();
			if (FBComp && FBComp->GetIsReplicated())
			{
				UE_LOG(LogPaper2DPlus, Warning,
					TEXT("Flipbook component '%s' on '%s' replicates (engine SourceFlipbook replication) while Paper2DPlus anim replication is enabled — the two are mutually exclusive (TASK-57 KTD-23). Disable one or the other."),
					*FBComp->GetName(), *GetName());
			}
			if (!bEventDrivenFlipbookDetection)
			{
				UE_LOG(LogPaper2DPlus, Warning,
					TEXT("'%s' is networked but running the stock-flipbook slow-poll path — anim-state publishes quantize to the 20Hz poll (up to 50 ms of latency). Use UPaper2DPlusFlipbookComponent for synchronous publishes (TASK-57)."),
					*GetName());
			}
		}
	}

	// Apply any replicated anim state that arrived before BeginPlay (initial bunches can precede it)
	// — DEFERRED one tick via the core ticker (the repo's deferred-PostLoad idiom): initial-bunch
	// RepNotifies fire BEFORE BeginPlay, and component BeginPlay runs BEFORE the actor's BP Event
	// BeginPlay, so an immediate drain here would broadcast the advisory to ZERO subscribers. One-shot
	// (returns false), weak-this so GC/teardown invalidates the capture; registered only when a stash
	// is actually pending. A binder that misses even the deferred drain pulls via
	// RebroadcastReplicatedAnimState (the documented bind-then-pull pattern).
	if (PendingRepAnimState.IsSet())
	{
		TWeakObjectPtr<UPaper2DPlusCharacterProfileComponent> WeakThis(this);
		FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateLambda([WeakThis](float /*DeltaTime*/)
			{
				if (UPaper2DPlusCharacterProfileComponent* Comp = WeakThis.Get())
				{
					Comp->RetryPendingRepAnimState();
				}
				return false; // one-shot
			}));
	}
}

void UPaper2DPlusCharacterProfileComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// End any active hit-stop BEFORE tearing down — a dying attacker must never leave the victim
	// frozen (this holds even when the destruction happens INSIDE an OnHitStopBegin handler:
	// restoration is never gated, only the nested End broadcast is suppressed), and the core
	// ticker must not outlive the component (TASK-76 PR4).
	CancelHitStop();

	// Close any open auto-detection window and drop the armed-set registration (no whiff — a dying
	// attacker is teardown, not a miss) — TASK-145.
	FinalizeAutoHitDetectionForMoveEnd(/*bBroadcastWhiff=*/false);

	// A cue state always receives a matching End, even when the component disappears before the
	// animation advances again. The shared funnel snapshots the authored union while the owning
	// assets are still rooted.
	ClaimFrameCueTerminal(
		EPaper2DPlusFrameCueEndReason::ComponentDestroyed,
		RuntimeFrameCueContext.PlaybackComponent,
		FrameCuePlaybackGeneration,
		/*bAllowFinalFrameBeforeDrain=*/false,
		/*bRequirePublicSourceMatch=*/false);
	DrainPendingFrameCueTerminal();

	if (bRegisterWithHitboxSubsystem)
	{
		if (UWorld* World = GetWorld())
		{
			if (UPaper2DPlusHitboxSubsystem* HitboxSubsystem = World->GetSubsystem<UPaper2DPlusHitboxSubsystem>())
			{
				HitboxSubsystem->UnregisterProfileComponent(this);
			}
		}
	}

	UnbindFrameCuePlaybackSource();

	// Drop the composed Layer gameplay tier with play — the
	// digest, the composed box/event views, and the pending flag are all transient equip state, and a
	// component that lingers past EndPlay (or re-begins) must never serve stale Layer boxes or hold
	// raw pointers into a layer asset it no longer has any liveness contract with. The layer render
	// component's own teardown CLEAR (its EndPlay/OnComponentDestroyed) already force-ended in-flight
	// Layer-owned Cue States while the asset was alive; this is the state reset half.
	bComposedCombatActive = false;
	bHasAppearanceCombatDigest = false;
	bPendingAppearanceCombatDirty = false;
	ComposedCombatFrames.Empty();
	ComposedLayerFrameCues.Empty();
	// The latch derived from those composed cues goes with them: a component that re-begins play (or
	// lingers) must re-observe a Layer cue before claiming one, exactly as a fresh component would.
	bObservedComposedLayerCues = false;
	WarmedFrameCueEffectFlipbooks.Empty();
	ResetDirectionalVariantWarm();
	AppearanceDigest = FPaper2DPlusAppearanceDescriptor();
	AppearanceDigestLayerAsset = nullptr;
	NotifyFrameCueSourceEnded();

	Super::EndPlay(EndPlayReason);
}

void UPaper2DPlusCharacterProfileComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	ClaimFrameCueTerminal(
		EPaper2DPlusFrameCueEndReason::ComponentDestroyed,
		RuntimeFrameCueContext.PlaybackComponent,
		FrameCuePlaybackGeneration,
		/*bAllowFinalFrameBeforeDrain=*/false,
		/*bRequirePublicSourceMatch=*/false);
	DrainPendingFrameCueTerminal();
	UnbindFrameCuePlaybackSource();
	WarmedFrameCueEffectFlipbooks.Empty();
	ResetDirectionalVariantWarm();
	NotifyFrameCueSourceEnded();
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

void UPaper2DPlusCharacterProfileComponent::NotifyFrameCueSourceEnded()
{
	if (!bFrameCueSourceEndedBroadcast)
	{
		bFrameCueSourceEndedBroadcast = true;
		OnFrameCueSourceEndedNative.Broadcast();
		OnFrameCueSourceEndedNative.Clear();
	}
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

void UPaper2DPlusCharacterProfileComponent::SetFrameCuePlaybackSource(
	UPaperFlipbookComponent* NewPlaybackSource)
{
	if (!HasBegunPlay())
	{
		FlipbookComponent = NewPlaybackSource;
		return;
	}

	if (BoundFrameCuePlaybackSource == NewPlaybackSource
		&& FlipbookComponent == NewPlaybackSource)
	{
		return;
	}

	ClaimFrameCueTerminal(
		EPaper2DPlusFrameCueEndReason::SourceRemoved,
		RuntimeFrameCueContext.PlaybackComponent,
		FrameCuePlaybackGeneration,
		/*bAllowFinalFrameBeforeDrain=*/false,
		/*bRequirePublicSourceMatch=*/false);
	if (bDispatchingFrameCues || bHandlingFlipbookChange || bHasPendingFrameCueTerminal)
	{
		PendingFrameCuePlaybackSource = NewPlaybackSource;
		bHasPendingFrameCuePlaybackSource = true;
		return;
	}

	ApplyFrameCuePlaybackSource(NewPlaybackSource);
}

void UPaper2DPlusCharacterProfileComponent::ApplyFrameCuePlaybackSource(
	UPaperFlipbookComponent* NewPlaybackSource)
{
	UnbindFrameCuePlaybackSource();
	FlipbookComponent = NewPlaybackSource;
	BindFrameCuePlaybackSource(NewPlaybackSource);

	// Reuse the single cache/initial-frame funnel. A null source also clears every cached view but
	// does not create a new playable generation.
	HandleFlipbookChanged(NewPlaybackSource ? NewPlaybackSource->GetFlipbook() : nullptr);

	// Late flipbook assignment re-runs the placement funnel: the new component carries its OWN authored
	// transform, so the capture rebases onto it rather than restoring the previous component's pose.
	ApplyProfileRelativeTransform();
}

void UPaper2DPlusCharacterProfileComponent::BindFrameCuePlaybackSource(
	UPaperFlipbookComponent* Source)
{
	BoundFrameCuePlaybackSource = Source;
	++FrameCuePlaybackBindingEpoch;
	bEventDrivenFlipbookDetection = false;
	bBoundStockNaturalCompletionLatched = false;
	bHasObservedFrameCuePlayingState = Source != nullptr;
	bObservedFrameCuePlaying = Source && Source->IsPlaying();

	if (UPaper2DPlusFlipbookComponent* Paper2DPlusSource =
		Cast<UPaper2DPlusFlipbookComponent>(Source))
	{
		BoundFlipbookChangedHandle = Paper2DPlusSource->OnFlipbookChangedNative.AddUObject(
			this, &UPaper2DPlusCharacterProfileComponent::HandleBoundFlipbookChanged);
		BoundFrameChangedHandle = Paper2DPlusSource->OnFrameChangedNative.AddUObject(
			this, &UPaper2DPlusCharacterProfileComponent::HandleBoundFrameChanged);
		BoundPlaybackStartedHandle = Paper2DPlusSource->OnPlaybackStartedNative.AddUObject(
			this, &UPaper2DPlusCharacterProfileComponent::HandleBoundPlaybackStarted);
		BoundPlaybackObservedHandle = Paper2DPlusSource->OnPlaybackObservedNative.AddUObject(
			this, &UPaper2DPlusCharacterProfileComponent::HandleBoundPlaybackObserved);
		BoundPlaybackNaturalFinishHandle =
			Paper2DPlusSource->OnNaturalFinishDetectedNative.AddUObject(
				this,
				&UPaper2DPlusCharacterProfileComponent::
					HandleBoundPlaybackNaturalFinishDetected);
		BoundPlaybackTerminalHandle = Paper2DPlusSource->OnPlaybackTerminalNative.AddUObject(
			this, &UPaper2DPlusCharacterProfileComponent::HandleBoundPlaybackTerminal);
		BoundPlaybackSourceDestroyedHandle =
			Paper2DPlusSource->OnPlaybackSourceDestroyedNative.AddUObject(
				this,
				&UPaper2DPlusCharacterProfileComponent::HandleBoundPlaybackSourceDestroyed);
		bEventDrivenFlipbookDetection = true;
	}
	else if (Source)
	{
		BoundStockPlaybackObserver =
			NewObject<UPaper2DPlusFrameCueStockPlaybackObserver>(this);
		BoundStockPlaybackObserver->Initialize(this, Source, FrameCuePlaybackBindingEpoch);
		Source->OnFinishedPlaying.AddDynamic(
			BoundStockPlaybackObserver.Get(),
			&UPaper2DPlusFrameCueStockPlaybackObserver::HandleFinishedPlaying);
	}
}

void UPaper2DPlusCharacterProfileComponent::UnbindFrameCuePlaybackSource()
{
	UPaperFlipbookComponent* const BoundSource = BoundFrameCuePlaybackSource.Get();
	if (IsValid(BoundSource))
	{
		if (UPaper2DPlusFlipbookComponent* Paper2DPlusSource =
			Cast<UPaper2DPlusFlipbookComponent>(BoundSource))
		{
			Paper2DPlusSource->OnFlipbookChangedNative.Remove(BoundFlipbookChangedHandle);
			Paper2DPlusSource->OnFrameChangedNative.Remove(BoundFrameChangedHandle);
			Paper2DPlusSource->OnPlaybackStartedNative.Remove(BoundPlaybackStartedHandle);
			Paper2DPlusSource->OnPlaybackObservedNative.Remove(BoundPlaybackObservedHandle);
			Paper2DPlusSource->OnNaturalFinishDetectedNative.Remove(
				BoundPlaybackNaturalFinishHandle);
			Paper2DPlusSource->OnPlaybackTerminalNative.Remove(BoundPlaybackTerminalHandle);
			Paper2DPlusSource->OnPlaybackSourceDestroyedNative.Remove(
				BoundPlaybackSourceDestroyedHandle);
		}
		else if (BoundStockPlaybackObserver)
		{
			BoundSource->OnFinishedPlaying.RemoveDynamic(
				BoundStockPlaybackObserver.Get(),
				&UPaper2DPlusFrameCueStockPlaybackObserver::HandleFinishedPlaying);
		}
	}
	if (BoundStockPlaybackObserver)
	{
		BoundStockPlaybackObserver->Detach();
	}

	BoundFlipbookChangedHandle.Reset();
	BoundFrameChangedHandle.Reset();
	BoundPlaybackStartedHandle.Reset();
	BoundPlaybackObservedHandle.Reset();
	BoundPlaybackNaturalFinishHandle.Reset();
	BoundPlaybackTerminalHandle.Reset();
	BoundPlaybackSourceDestroyedHandle.Reset();
	BoundStockPlaybackObserver = nullptr;
	BoundFrameCuePlaybackSource = nullptr;
	bEventDrivenFlipbookDetection = false;
	bHasObservedFrameCuePlayingState = false;
	bObservedFrameCuePlaying = false;
	bBoundStockNaturalCompletionLatched = false;
	bNativePlaybackStartPendingAfterExternalTerminal = false;
	++FrameCuePlaybackBindingEpoch;
}

bool UPaper2DPlusCharacterProfileComponent::ValidateBoundFrameCuePlaybackSource(
	const UPaperFlipbookComponent* ExpectedSource,
	const TCHAR* Operation)
{
	UPaperFlipbookComponent* BoundSource = BoundFrameCuePlaybackSource.Get();
	// Worldless automation and construction-time callers have no BeginPlay binding. Their explicitly
	// assigned property is still a coherent expected source for the public lifecycle seam.
	if (!BoundSource && !HasBegunPlay())
	{
		BoundSource = FlipbookComponent.Get();
	}

	if (!IsValid(ExpectedSource)
		|| ExpectedSource != BoundSource
		|| ExpectedSource != FlipbookComponent.Get())
	{
		UE_LOG(
			LogPaper2DPlus,
			Warning,
			TEXT("%s ignored on '%s': expected playback source '%s' does not match bound/public source '%s'/'%s'. Use SetFrameCuePlaybackSource after BeginPlay."),
			Operation ? Operation : TEXT("Frame Cue lifecycle operation"),
			*GetName(),
			*GetNameSafe(ExpectedSource),
			*GetNameSafe(BoundSource),
			*GetNameSafe(FlipbookComponent.Get()));

		// A legacy direct C++ property write is detectable when an old callback or the stock poll next
		// runs. End/unbind/rebind through the supported seam, but never reinterpret this stale callback
		// as belonging to the destination source.
		if (HasBegunPlay()
			&& BoundFrameCuePlaybackSource != FlipbookComponent
			&& !bDispatchingFrameCues
			&& !bHandlingFlipbookChange)
		{
			UPaperFlipbookComponent* DirectlyAssignedSource = FlipbookComponent.Get();
			SetFrameCuePlaybackSource(DirectlyAssignedSource);
		}
		return false;
	}
	return true;
}

void UPaper2DPlusCharacterProfileComponent::HandleBoundFlipbookChanged(
	UPaper2DPlusFlipbookComponent* Source,
	UPaperFlipbook* NewFlipbook)
{
	if (ValidateBoundFrameCuePlaybackSource(Source, TEXT("Flipbook-change callback")))
	{
		HandleFlipbookChanged(NewFlipbook);
	}
}

void UPaper2DPlusCharacterProfileComponent::HandleBoundFrameChanged(
	UPaper2DPlusFlipbookComponent* Source,
	const int32 NewFrame)
{
	if (ValidateBoundFrameCuePlaybackSource(Source, TEXT("Frame-change callback")))
	{
		HandleFrameChanged(NewFrame);
	}
}

void UPaper2DPlusCharacterProfileComponent::HandleBoundPlaybackStarted(
	UPaper2DPlusFlipbookComponent* Source)
{
	if (!ValidateBoundFrameCuePlaybackSource(Source, TEXT("Playback-start callback")))
	{
		return;
	}
	if (bExternalFrameCuePlaybackGeneration)
	{
		bNativePlaybackStartPendingAfterExternalTerminal =
			bFrameCueTerminalAccepted && bHasPendingFrameCueTerminal;
		return;
	}
	if (FrameCuePlaybackGeneration > 0 && !bFrameCueTerminalAccepted)
	{
		// A source may be assigned while stopped and played later. Its SetFlipbook warm already opened
		// the generation and evaluated frame 0; the first Play is not a same-flipbook restart.
		bHasObservedFrameCuePlayingState = true;
		bObservedFrameCuePlaying = true;
		return;
	}

	BeginFrameCuePlaybackGeneration(
		Source,
		/*bExternalOwner=*/false,
		/*bForceCurrentFrameEvaluation=*/true);
}

void UPaper2DPlusCharacterProfileComponent::HandleBoundPlaybackObserved(
	UPaper2DPlusFlipbookComponent* Source)
{
	// Keep the Profile's admitted lifecycle authoritative even when playback starts and stops between
	// two custom-component observations (for example, PlayFromStart inside an external Cue End followed
	// by Stop before the next tick). The custom component's own stopped->stopped sample cannot recover
	// that pulse, while this pre-Super observation still sees the Profile's committed playing state.
	ProcessObservedFrameCuePlaybackState(Source);
}

void UPaper2DPlusCharacterProfileComponent::HandleBoundPlaybackNaturalFinishDetected(
	UPaper2DPlusFlipbookComponent* Source)
{
	if (!ValidateBoundFrameCuePlaybackSource(Source, TEXT("Natural-finish callback"))
		|| bExternalFrameCuePlaybackGeneration)
	{
		return;
	}
	EarlyCustomNaturalFinishGeneration = FrameCuePlaybackGeneration;

	// Claim Completed before arbitrary inherited OnFinishedPlaying listeners can switch Profile or
	// flipbook, but leave the transaction queued until the custom component delivers its post-Super
	// final frame.
	const bool bClaimed = ClaimFrameCueTerminal(
		EPaper2DPlusFrameCueEndReason::Completed,
		Source,
		FrameCuePlaybackGeneration,
		/*bAllowFinalFrameBeforeDrain=*/true);
	if (!bClaimed)
	{
		return;
	}

	// Unreal invokes OnFinishedPlaying before CalculateCurrentFrame. Evaluate the terminal playback
	// position through the same HandleFrameChanged funnel now, while the outgoing tuple is still
	// authoritative and before later inherited finish listeners can restart/switch/destroy it.
	// The custom component's post-Super frame callback is then harmlessly rejected by the claimed
	// generation, so the final frame is never delivered twice.
	int32 FinalFrame = INDEX_NONE;
	if (UPaperFlipbook* FinishingFlipbook = Source->GetFlipbook())
	{
		FinalFrame = FinishingFlipbook->GetKeyFrameIndexAtTime(Source->GetPlaybackPosition());
		if (FinalFrame == INDEX_NONE && FinishingFlipbook->GetNumKeyFrames() > 0)
		{
			FinalFrame = Source->GetPlayRate() < 0.0f
				? 0
				: FinishingFlipbook->GetNumKeyFrames() - 1;
		}
	}

	if (FinalFrame != INDEX_NONE
		&& FinalFrame != RuntimeFrameCueContext.CurrentFrame)
	{
		HandleFrameChanged(FinalFrame);
	}
	else
	{
		bPendingTerminalAllowsFinalFrame = false;
		DrainPendingFrameCueTerminal();
		FlushPendingAnimationDispatch();
	}
}

void UPaper2DPlusCharacterProfileComponent::HandleBoundPlaybackTerminal(
	UPaper2DPlusFlipbookComponent* Source,
	const bool bCompletedNaturally)
{
	if (!ValidateBoundFrameCuePlaybackSource(Source, TEXT("Playback-terminal callback"))
		|| bExternalFrameCuePlaybackGeneration)
	{
		return;
	}
	if (bCompletedNaturally && EarlyCustomNaturalFinishGeneration > 0)
	{
		// The early OnFinished signal already claimed (and normally drained) the outgoing generation.
		// A final-frame handler may have opened a replacement generation before this post-Super marker
		// arrives, so never reinterpret it against the now-current token.
		EarlyCustomNaturalFinishGeneration = 0;
		DrainPendingFrameCueTerminal();
		FlushPendingAnimationDispatch();
		return;
	}

	ClaimFrameCueTerminal(
		bCompletedNaturally
			? EPaper2DPlusFrameCueEndReason::Completed
			: EPaper2DPlusFrameCueEndReason::PlaybackStopped,
		Source,
		FrameCuePlaybackGeneration);
	DrainPendingFrameCueTerminal();
	FlushPendingAnimationDispatch();
}

void UPaper2DPlusCharacterProfileComponent::HandleBoundStockPlaybackFinished(
	UPaperFlipbookComponent* Source,
	const uint64 BindingEpoch)
{
	if (BindingEpoch != FrameCuePlaybackBindingEpoch
		|| !ValidateBoundFrameCuePlaybackSource(Source, TEXT("Stock natural-finish callback"))
		|| bExternalFrameCuePlaybackGeneration)
	{
		return;
	}

	if (!ClaimFrameCueTerminal(
			EPaper2DPlusFrameCueEndReason::Completed,
			Source,
			FrameCuePlaybackGeneration,
			/*bAllowFinalFrameBeforeDrain=*/true))
	{
		return;
	}
	bBoundStockNaturalCompletionLatched = true;

	// The engine broadcasts OnFinishedPlaying before refreshing its cached key frame. Resolve the
	// outgoing snapshot's terminal key frame now and dispatch it before later finish listeners can
	// restart, switch, or tear down the stock component.
	int32 FinalFrame = INDEX_NONE;
	if (const UPaperFlipbook* FinishingFlipbook = RuntimeFrameCueContext.Flipbook)
	{
		if (FinishingFlipbook->GetNumKeyFrames() > 0)
		{
			FinalFrame = Source->GetPlayRate() < 0.0f
				? 0
				: FinishingFlipbook->GetNumKeyFrames() - 1;
		}
	}
	if (FinalFrame != INDEX_NONE && FinalFrame != RuntimeFrameCueContext.CurrentFrame)
	{
		HandleFrameChanged(FinalFrame);
	}
	else
	{
		bPendingTerminalAllowsFinalFrame = false;
		DrainPendingFrameCueTerminal();
		FlushPendingAnimationDispatch();
	}
}

void UPaper2DPlusCharacterProfileComponent::HandleBoundPlaybackSourceDestroyed(
	UPaper2DPlusFlipbookComponent* Source)
{
	if (Source != BoundFrameCuePlaybackSource.Get())
	{
		return;
	}

	ClaimFrameCueTerminal(
		EPaper2DPlusFrameCueEndReason::SourceRemoved,
		Source,
		FrameCuePlaybackGeneration,
		/*bAllowFinalFrameBeforeDrain=*/false,
		/*bRequirePublicSourceMatch=*/false);
	DrainPendingFrameCueTerminal();
	FlushPendingAnimationDispatch();

	if (Source == BoundFrameCuePlaybackSource.Get())
	{
		ApplyFrameCuePlaybackSource(nullptr);
	}
}

int64 UPaper2DPlusCharacterProfileComponent::BeginExternalFrameCuePlayback(
	UPaperFlipbookComponent* ExpectedSource)
{
	if (!ValidateBoundFrameCuePlaybackSource(
			ExpectedSource,
			TEXT("BeginExternalFrameCuePlayback"))
		|| !ExpectedSource->GetFlipbook())
	{
		return 0;
	}

	if (CachedRuntimeFlipbook.Get() != ExpectedSource->GetFlipbook()
		|| RuntimeFrameCueContext.CharacterProfile != CharacterProfile)
	{
		HandleFlipbookChanged(ExpectedSource->GetFlipbook());
		if (bHasPendingFrameCueTerminal || bHasPendingFlipbookChange)
		{
			return 0;
		}
		bExternalFrameCuePlaybackGeneration = true;
		return FrameCuePlaybackGeneration;
	}

	ClaimFrameCueTerminal(
		EPaper2DPlusFrameCueEndReason::Interrupted,
		ExpectedSource,
		FrameCuePlaybackGeneration);
	DrainPendingFrameCueTerminal();
	FlushPendingAnimationDispatch();
	if (bHasPendingFrameCueTerminal)
	{
		// A Begin requested from inside Cue delivery cannot open the replacement generation until
		// the claimed outgoing terminal drains. Fail closed; the external owner can retry afterward.
		return 0;
	}
	BeginFrameCuePlaybackGeneration(
		ExpectedSource,
		/*bExternalOwner=*/true,
		/*bForceCurrentFrameEvaluation=*/true);
	return FrameCuePlaybackGeneration;
}

bool UPaper2DPlusCharacterProfileComponent::ReportFrameCuePlaybackStopped(
	UPaperFlipbookComponent* ExpectedSource,
	const int64 ExpectedGeneration)
{
	const bool bClaimed = ClaimFrameCueTerminal(
		EPaper2DPlusFrameCueEndReason::PlaybackStopped,
		ExpectedSource,
		ExpectedGeneration);
	if (bClaimed)
	{
		DrainPendingFrameCueTerminal();
		FlushPendingAnimationDispatch();
	}
	return bClaimed;
}

bool UPaper2DPlusCharacterProfileComponent::ReportFrameCuePlaybackCompleted(
	UPaperFlipbookComponent* ExpectedSource,
	const int64 ExpectedGeneration)
{
	const bool bClaimed = ClaimFrameCueTerminal(
		EPaper2DPlusFrameCueEndReason::Completed,
		ExpectedSource,
		ExpectedGeneration);
	if (bClaimed)
	{
		DrainPendingFrameCueTerminal();
		FlushPendingAnimationDispatch();
	}
	return bClaimed;
}

void UPaper2DPlusCharacterProfileComponent::BeginFrameCuePlaybackGeneration(
	UPaperFlipbookComponent* Source,
	const bool bExternalOwner,
	const bool bForceCurrentFrameEvaluation)
{
	if (!Source || !Source->GetFlipbook() || bHasPendingFrameCueTerminal)
	{
		return;
	}

	if (FrameCuePlaybackGeneration == MAX_int64)
	{
		FrameCuePlaybackGeneration = 1;
	}
	else
	{
		++FrameCuePlaybackGeneration;
	}

	bFrameCueTerminalAccepted = false;
	bPendingTerminalAllowsFinalFrame = false;
	bExternalFrameCuePlaybackGeneration = bExternalOwner;
	bNativePlaybackStartPendingAfterExternalTerminal = false;
	bHasObservedFrameCuePlayingState = true;
	bObservedFrameCuePlaying = Source->IsPlaying();
	bBoundStockNaturalCompletionLatched = false;
	LatchedOutRangeCues.Empty();
	RefreshRuntimeFrameCueContextIdentity();

	if (bForceCurrentFrameEvaluation)
	{
		PreviousFrameIndex = INDEX_NONE;
		const int32 CurrentFrame =
			Source->GetFlipbook()->GetKeyFrameIndexAtTime(Source->GetPlaybackPosition());
		if (CurrentFrame != INDEX_NONE)
		{
			HandleFrameChanged(CurrentFrame);
		}
	}
}

void UPaper2DPlusCharacterProfileComponent::RefreshRuntimeFrameCueContextIdentity()
{
	RuntimeFrameCueContext.OwningActor = GetOwner();
	RuntimeFrameCueContext.ProfileComponent = this;
	RuntimeFrameCueContext.CharacterProfile = CharacterProfile;
	RuntimeFrameCueContext.PlaybackComponent = BoundFrameCuePlaybackSource
		? BoundFrameCuePlaybackSource.Get()
		: FlipbookComponent.Get();
	RuntimeFrameCueContext.Flipbook = CachedRuntimeFlipbook.Get();
	RuntimeFrameCueContext.AnimationName = FName(*CachedMoveNameForCompose);
	RuntimeFrameCueContext.NetContext = ResolveNetContext();
	RuntimeFrameCueContext.SetEvaluationMode(
		bApplyingReplicatedState
			? EPaper2DPlusFrameCueEvaluationMode::RuntimeCatchUp
			: EPaper2DPlusFrameCueEvaluationMode::RuntimePlayback);
}

FPaper2DPlusFrameCueContext
UPaper2DPlusCharacterProfileComponent::MakeRuntimeFrameCueContext(
	const int32 CurrentFrame,
	const int32 PreviousFrame,
	const bool bWasLoopWrap,
	const EPaper2DPlusNetContext NetContext,
	const EPaper2DPlusFrameCueEvaluationMode EvaluationMode) const
{
	FPaper2DPlusFrameCueContext Context = RuntimeFrameCueContext;
	Context.CurrentFrame = CurrentFrame;
	Context.PreviousFrame = PreviousFrame;
	Context.bWasLoopWrap = bWasLoopWrap;
	Context.NetContext = NetContext;
	Context.Phase = EPaper2DPlusFrameCuePhase::Trigger;
	Context.EndReason = EPaper2DPlusFrameCueEndReason::None;
	Context.bIsCompressed = false;
	Context.SetEvaluationMode(EvaluationMode);
	return Context;
}

bool UPaper2DPlusCharacterProfileComponent::ClaimFrameCueTerminal(
	const EPaper2DPlusFrameCueEndReason EndReason,
	UPaperFlipbookComponent* ExpectedSource,
	const int64 ExpectedGeneration,
	const bool bAllowFinalFrameBeforeDrain,
	const bool bRequirePublicSourceMatch)
{
	UPaperFlipbookComponent* EffectiveBoundSource = BoundFrameCuePlaybackSource.Get();
	if (!EffectiveBoundSource && !HasBegunPlay())
	{
		EffectiveBoundSource = FlipbookComponent.Get();
	}
	const bool bSourceMatchesBinding =
		ExpectedSource
		&& ExpectedSource == EffectiveBoundSource
		&& (!bRequirePublicSourceMatch || IsValid(ExpectedSource));
	if (EndReason == EPaper2DPlusFrameCueEndReason::None
		|| ExpectedGeneration <= 0
		|| ExpectedGeneration != FrameCuePlaybackGeneration
		|| bFrameCueTerminalAccepted
		|| !bSourceMatchesBinding
		|| (bRequirePublicSourceMatch
			&& !ValidateBoundFrameCuePlaybackSource(
				ExpectedSource,
				TEXT("Frame Cue terminal report"))))
	{
		return false;
	}

	// Open -> TerminalClaimed is committed before any Cue behavior/listener can re-enter. Every later
	// boundary for this generation is therefore a no-op and cannot relabel the End.
	bFrameCueTerminalAccepted = true;
	bHasPendingFrameCueTerminal = true;
	bPendingTerminalAllowsFinalFrame = bAllowFinalFrameBeforeDrain;
	PendingFrameCueTerminalReason = EndReason;
	PendingFrameCueTerminalGeneration = ExpectedGeneration;
	PendingFrameCueTerminalSource = ExpectedSource;
	PendingFrameCueTerminalContext = RuntimeFrameCueContext;
	if (PendingFrameCueTerminalContext.CurrentFrame == INDEX_NONE)
	{
		PendingFrameCueTerminalContext.CurrentFrame = CachedCurrentFrameIndex;
	}
	if (PendingFrameCueTerminalContext.PreviousFrame == INDEX_NONE)
	{
		PendingFrameCueTerminalContext.PreviousFrame = PreviousFrameIndex;
	}
	PendingFrameCueTerminalOrder.Reset();
	if (CachedFrameEventData)
	{
		PendingFrameCueTerminalOrder = CachedFrameEventData->FrameCues;
	}
	// The composed view is deliberately GC-invisible and its pointers are safe only behind a live
	// digest asset (see ComposedLayerFrameCues). Once that asset has died the view may already point at
	// freed objects, and PendingFrameCueTerminalOrder is a STRONG UPROPERTY the GC will scan — so an
	// orphaned view must never enter it. Its actives are dropped by pointer identity at drain
	// (PurgeDanglingComposedRangeCuesIfDigestAssetDied) without ever being dereferenced.
	if (ComposedLayerFrameCues.Num() == 0 || AppearanceDigestLayerAsset.IsValid())
	{
		PendingFrameCueTerminalOrder.Append(ComposedLayerFrameCues);
	}

	if (!bDispatchingFrameCues
		&& !bHandlingFlipbookChange
		&& !bAllowFinalFrameBeforeDrain)
	{
		DrainPendingFrameCueTerminal();
	}
	return true;
}

void UPaper2DPlusCharacterProfileComponent::DrainPendingFrameCueTerminal()
{
	if (!bHasPendingFrameCueTerminal
		|| bDispatchingFrameCues
		|| bHandlingFlipbookChange)
	{
		return;
	}

	bPendingTerminalAllowsFinalFrame = false;
	const EPaper2DPlusFrameCueEndReason EndReason = PendingFrameCueTerminalReason;
	const bool bDrainingExternalGeneration =
		bExternalFrameCuePlaybackGeneration
		&& PendingFrameCueTerminalGeneration == FrameCuePlaybackGeneration;
	const bool bWasObservedPlaying = bObservedFrameCuePlaying;
	const TWeakObjectPtr<UPaperFlipbookComponent> TerminalSource =
		PendingFrameCueTerminalSource;
	PendingFrameCueTerminalReason = EPaper2DPlusFrameCueEndReason::None;

	// Purge dead Layer pointers before entering the retained authored order. Live objects are strongly
	// held by PendingFrameCueTerminalOrder until every End has been delivered.
	PurgeDanglingComposedRangeCuesIfDigestAssetDied();
	{
		TGuardValue<bool> ReentryGuard(bDispatchingFrameCues, true);

		// A Completed terminal is the ONE place the final frame's trailing boundary provably
		// crossed: the ordinary transition funnel fires end-anchored cues only for DEPARTED
		// frames, so the frame playback finishes on can never fire there. Fire those moments
		// here, BEFORE the range teardown (mirroring the funnel's moment-before-range order),
		// through the same net gate as ordinary dispatch. Every other terminal reason means the
		// frame was cut short, and a cut-short frame deliberately keeps its end-anchored silence.
		if (EndReason == EPaper2DPlusFrameCueEndReason::Completed
			&& !PendingFrameCueTerminalContext.bIsCatchUp
			&& PendingFrameCueTerminalContext.CurrentFrame != INDEX_NONE)
		{
			const UWorld* World = GetWorld();
			bool bDedicatedServer = World && World->GetNetMode() == NM_DedicatedServer;
#if !UE_BUILD_SHIPPING
			bDedicatedServer = DedicatedServerOverrideForTests.Get(bDedicatedServer);
#endif
			bool bOwnerLocallyControlled = false;
			if (World)
			{
				if (const APawn* OwnerPawn = Cast<APawn>(GetOwner()))
				{
					bOwnerLocallyControlled = OwnerPawn->IsLocallyControlled();
				}
			}
			FPaper2DPlusFrameCueContext TriggerContext = PendingFrameCueTerminalContext;
			TriggerContext.Phase = EPaper2DPlusFrameCuePhase::Trigger;
			TriggerContext.EndReason = EPaper2DPlusFrameCueEndReason::None;
			TriggerContext.bIsCompressed = false;
			for (const TObjectPtr<UPaper2DPlusCueBase>& CuePtr : PendingFrameCueTerminalOrder)
			{
				UPaper2DPlusCue* Moment = Cast<UPaper2DPlusCue>(CuePtr.Get());
				if (!Moment
					|| Moment->TriggerEdge != EPaper2DPlusCueTriggerEdge::FrameEnd
					|| Moment->TriggerFrame != TriggerContext.CurrentFrame
					|| !Paper2DPlusFrameCueBehavior::IsPlacementResolvable(Moment)
					|| !Paper2DPlusNetGating::ShouldDispatchFrameCue(
						Moment->NetPolicy,
						TriggerContext.NetContext,
						bDedicatedServer,
						bOwnerLocallyControlled,
						TriggerContext.bIsCatchUp))
				{
					continue;
				}
				NotifyFrameCue(*Moment, TriggerContext);
			}
		}

		if (ActiveRangeCues.Num() > 0)
		{
			Paper2DPlusFrameCues::ForceEndActiveRanges(
				PendingFrameCueTerminalOrder,
				PendingFrameCueTerminalContext,
				EndReason,
				ActiveRangeCues,
				[this](
					UPaper2DPlusCueBase& Cue,
					const FPaper2DPlusFrameCueContext& CueContext)
				{
					NotifyFrameCue(Cue, CueContext);
				});
		}
	}
	LatchedOutRangeCues.Empty();
	PendingFrameCueTerminalOrder.Reset();
	PendingFrameCueTerminalSource = nullptr;
	PendingFrameCueTerminalGeneration = 0;
	bHasPendingFrameCueTerminal = false;

	if (bDrainingExternalGeneration)
	{
		// External ownership lasts for one generation, not for the lifetime of the binding. Hand the
		// same source back to normal custom callbacks/stock observation after its accepted terminal.
		const bool bSourceStillBound =
			TerminalSource.IsValid()
			&& TerminalSource.Get() == BoundFrameCuePlaybackSource.Get();
		const bool bPlayingNow = bSourceStillBound && TerminalSource->IsPlaying();
		const bool bStartNativeGeneration =
			bNativePlaybackStartPendingAfterExternalTerminal
			|| (!bWasObservedPlaying && bPlayingNow);
		bExternalFrameCuePlaybackGeneration = false;
		bNativePlaybackStartPendingAfterExternalTerminal = false;
		bHasObservedFrameCuePlayingState = true;
		bObservedFrameCuePlaying = bPlayingNow;
		bBoundStockNaturalCompletionLatched = false;
		if (bStartNativeGeneration && bPlayingNow)
		{
			BeginFrameCuePlaybackGeneration(
				TerminalSource.Get(),
				/*bExternalOwner=*/false,
				/*bForceCurrentFrameEvaluation=*/true);
		}
	}
}

void UPaper2DPlusCharacterProfileComponent::ProcessObservedFrameCuePlaybackState(
	UPaperFlipbookComponent* Source)
{
	if (!ValidateBoundFrameCuePlaybackSource(Source, TEXT("Playback-state poll"))
		|| bExternalFrameCuePlaybackGeneration)
	{
		return;
	}

	const bool bPlayingNow = Source->IsPlaying();
	if (bBoundStockNaturalCompletionLatched)
	{
		bBoundStockNaturalCompletionLatched = false;
		bHasObservedFrameCuePlayingState = true;
		bObservedFrameCuePlaying = bPlayingNow;
		if (bPlayingNow && bFrameCueTerminalAccepted)
		{
			BeginFrameCuePlaybackGeneration(
				Source,
				/*bExternalOwner=*/false,
				/*bForceCurrentFrameEvaluation=*/true);
		}
		return;
	}
	if (!bHasObservedFrameCuePlayingState)
	{
		bHasObservedFrameCuePlayingState = true;
		bObservedFrameCuePlaying = bPlayingNow;
		return;
	}

	if (!bObservedFrameCuePlaying && bPlayingNow)
	{
		if (FrameCuePlaybackGeneration <= 0 || bFrameCueTerminalAccepted)
		{
			BeginFrameCuePlaybackGeneration(
				Source,
				/*bExternalOwner=*/false,
				/*bForceCurrentFrameEvaluation=*/true);
		}
	}
	else if (bObservedFrameCuePlaying && !bPlayingNow)
	{
		ClaimFrameCueTerminal(
			bBoundStockNaturalCompletionLatched
				? EPaper2DPlusFrameCueEndReason::Completed
				: EPaper2DPlusFrameCueEndReason::PlaybackStopped,
			Source,
			FrameCuePlaybackGeneration);
	}

	bObservedFrameCuePlaying = bPlayingNow;
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
	CachedRuntimeFlipbook = NewFlipbook;
	CachedCurrentFrameIndex = INDEX_NONE;
	bCachedLocalFrameDataValid = false;
	CachedLocalFrameData = FFrameHitboxData();
	CachedPivotFraction = FVector2D::ZeroVector;
	bCachedHasAttack = false;
	bCachedHasHurtbox = false;
	// Move-instance bookkeeping (TASK-57 U2) — UNCONDITIONAL, Standalone too (O(1); feeds
	// single-player hit dedup in U4, not just the wire): every flipbook change is a new move
	// instance, with a fresh once-per-window hit-dedup ledger. The wire Sequence is stamped from
	// this counter at publish time (with the sentinel-skip) so the producer invariant
	// Sequence == uint16(MoveInstanceCounter) holds at every publish.
	++MoveInstanceCounter;
	ProcessedHits.Reset();
	// Clear the per-tick frame span (TASK-57 U4) so a fresh move instance does NOT inherit the PREVIOUS
	// move's span: a ValidateAndRegisterHit / GetCurrentHitWindowIndex landing AFTER this OnFlipbookChanged
	// but BEFORE the new move's first HandleFrameChanged would otherwise walk the old move's span against
	// the new move's attack frames. With both INDEX_NONE the ResolveLatestInSpanAttackFrame fallback
	// re-anchors to the LIVE key frame until the new move records its own real span.
	LastFrameSpanBegin = INDEX_NONE;
	LastFrameSpanEnd = INDEX_NONE;
	MarkCachedWorldStateDirty();

	// Undo any currently applied sprite offset before switching flipbooks
	RetireAppliedSpriteOffset();

	// Resolve and cache the flipbook data + feature flags
	CachedCombatData = nullptr;
	CachedMotionData = nullptr;
	CachedFrameEventData = nullptr;
	CachedMoveNameForCompose.Reset();
	ResetDirectionalVariantWarm();

	if (CharacterProfile && NewFlipbook)
	{
		if (const FFlipbookProfileEntry* Entry = CharacterProfile->FindByFlipbookPtr(NewFlipbook))
		{
			Entry->GetCacheView(CachedCombatData, CachedMotionData, CachedFrameEventData);
			CachedMoveNameForCompose = Entry->Identity.FlipbookName;
			WarmDirectionalVariantArt(*Entry);
		}
	}

	// One-time runtime diagnostic: anything left in the legacy FrameEvents array is NEVER dispatched
	// (runtime reads only CachedFrameEventData FrameCues). The failure is still real, but no automatic
	// conversion exists any more — nothing rewrites these rows into Cues, so the remedy is re-authoring.
	// Warn once per component when a re-warmed animation still carries such rows; only on cache warm,
	// never per-frame. HasBegunPlay gates out CDO/archetype/editor-preview construction (never begins play).
	if (!bWarnedUndispatchedLegacyFrameEvents && HasBegunPlay()
		&& CachedFrameEventData && CachedFrameEventData->FrameEvents.Num() > 0)
	{
		bWarnedUndispatchedLegacyFrameEvents = true;
		const AActor* Owner = GetOwner();
		UE_LOG(LogPaper2DPlus, Warning,
			TEXT("Actor '%s' animation '%s' (profile '%s') has %d legacy Frame Event(s) that are NEVER dispatched at runtime. Nothing converts them automatically: re-author them as Frame Cues in the Character Profile's Frame Cues tab, then delete the legacy rows."),
			Owner ? *Owner->GetName() : TEXT("<none>"),
			*CachedMoveNameForCompose,
			CharacterProfile ? *CharacterProfile->GetName() : TEXT("<none>"),
			CachedFrameEventData->FrameEvents.Num());
	}

	// After the raw warm, recompose the Layer tier for the new animation from
	// the STORED committed digest — no push happens on a flipbook change (a mid-preview flipbook change
	// therefore still composes from committed state; the preview overlay never reaches the digest).
	// Non-layered actors never received a push, so this is skipped and the raw path stays byte-identical.
	if (bHasAppearanceCombatDigest)
	{
		RecomposeAppearanceCombatFrames();
	}
	WarmCurrentFrameCueEffects();

	// The animation is now fully warmed (base + composed Layer tier), which is the first moment this
	// component can tell whether anything it is about to dispatch is undeliverable.
	MaybeWarnUndispatchableFrameCuePlacements();

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

	// Publish TAIL (TASK-57 U2/KTD-8): after the cache warm, the authority snapshots the new state
	// onto the wire — this is the SINGLE publish funnel (plus the KTD-9 self-loop confirm site,
	// which cannot route here because the engine SetFlipbook identity early-out never fires
	// OnFlipbookChanged for A→A). No-op outside the Authority context.
	PublishAnimStateIfAuthority();
}

void UPaper2DPlusCharacterProfileComponent::HandleFlipbookChanged(UPaperFlipbook* NewFlipbook)
{
	// Re-entry guard: a Frame Cue receiver that synchronously calls
	// SetFlipbook queues the new flipbook until the outer dispatch is stable.
	if (bHandlingFlipbookChange || bDispatchingFrameCues || bHasPendingFrameCueTerminal)
	{
		ClaimFrameCueTerminal(
			EPaper2DPlusFrameCueEndReason::AnimationChanged,
			RuntimeFrameCueContext.PlaybackComponent,
			FrameCuePlaybackGeneration);
		QueuePendingFlipbookChange(NewFlipbook);
		return;
	}

	ClaimFrameCueTerminal(
		EPaper2DPlusFrameCueEndReason::AnimationChanged,
		RuntimeFrameCueContext.PlaybackComponent,
		FrameCuePlaybackGeneration);

	{
		TGuardValue<bool> ReentryGuard(bHandlingFlipbookChange, true);

		// Close the previous move's auto-detection window + evaluate the whiff BEFORE the cache swap
		// (the OLD move's name is still cached) — TASK-145.
		FinalizeAutoHitDetectionForMoveEnd(/*bBroadcastWhiff=*/true);

		OnFlipbookChanged(NewFlipbook);
		if (NewFlipbook)
		{
			BeginFrameCuePlaybackGeneration(
				BoundFrameCuePlaybackSource
					? BoundFrameCuePlaybackSource.Get()
					: FlipbookComponent.Get(),
				/*bExternalOwner=*/false,
				/*bForceCurrentFrameEvaluation=*/false);
		}
		else
		{
			RefreshRuntimeFrameCueContextIdentity();
		}

		// SetFlipbook resets playback position to 0. Compute the target frame but
		// do NOT set PreviousFrameIndex — HandleFrameChanged needs to see the
		// transition from INDEX_NONE (no previous frame) to frame 0 so that
		// Frame Cues at frame 0 fire correctly.
		PreviousFrameIndex = INDEX_NONE;
	}

	// Dispatch target: normally frame 0, but the apply-mode catch-up warm (TASK-57 U8) routes a
	// MID-MOVE key frame here so a simulated proxy reconstructs the move at the server-anchored
	// position — one-shots stay suppressed (bApplyingReplicatedState) and ranged events whose range
	// contains the target frame Begin with bIsCatchUp=true. PendingCatchUpTargetFrame is held by a
	// TGuardValue across the SetFlipbook in ApplyReplicatedFlipbookToProxy.
	const int32 DispatchTargetFrame = (PendingCatchUpTargetFrame != INDEX_NONE)
		? PendingCatchUpTargetFrame
		: (NewFlipbook ? NewFlipbook->GetKeyFrameIndexAtTime(0.0f) : INDEX_NONE);
	if (DispatchTargetFrame == INDEX_NONE)
	{
		FlushPendingAnimationDispatch();
		return;
	}

	// Single source of truth: funnel through HandleFrameChanged (root motion, effects, AND frame
	// events all go through one path).
	HandleFrameChanged(DispatchTargetFrame);
	FlushPendingAnimationDispatch();
}

/*
 * Frame Cue dispatch — called every time the flipbook advances to a new key frame. Cues emit
 * Trigger; Cue States pair Begin/optional Update/End through ActiveRangeCues.
 *
 * Root motion: computes delta from authored position curve (RootMotion[Frame].Position).
 *   Delta = CurrentPos - LastAppliedPos, scaled by flipbook component world scale.
 *   Applied via AddWorldOffset on the owning actor.
 *
 * Re-entry guard: bDispatchingFrameCues prevents flipbook changes during dispatch
 *   from causing recursive HandleFrameChanged calls.
 */
void UPaper2DPlusCharacterProfileComponent::HandleFrameChanged(int32 NewFrame)
{
	if (NewFrame == INDEX_NONE) return;

	// A terminal claim rejects stale frame callbacks. Natural completion is the sole exception: the
	// early OnFinished claim reserves Completed, then allows exactly the definitive final frame before
	// the transaction drains.
	if (bFrameCueTerminalAccepted && !bPendingTerminalAllowsFinalFrame)
	{
		// An observed frame ADVANCE after the terminal drained is not stale — it is playback this
		// component cannot detect any other way. PaperZD (and any manual driver) calls Stop() once and
		// then drives the sprite by SetPlaybackPosition, so IsPlaying() never flips back and the
		// stopped→playing observation that normally reopens a generation never fires; without this
		// reopen, one observed stop would permanently silence every later cue on the component. The
		// reopen stays out of a live drain (the claim's final-frame decision stands), out of an
		// external generation (its owner keeps sole session authority), and out of dispatch/flipbook
		// teardown re-entry (the next observed frame reopens instead).
		if (!bHasPendingFrameCueTerminal
			&& !bExternalFrameCuePlaybackGeneration
			&& !bDispatchingFrameCues
			&& !bHandlingFlipbookChange)
		{
			UPaperFlipbookComponent* MovingSource = BoundFrameCuePlaybackSource.Get();
			if (!MovingSource)
			{
				MovingSource = GetResolvedFlipbookComponent();
			}
			if (MovingSource && MovingSource->GetFlipbook())
			{
				BeginFrameCuePlaybackGeneration(
					MovingSource,
					/*bExternalOwner=*/false,
					/*bForceCurrentFrameEvaluation=*/true);
			}
		}
		return;
	}

	if (bDispatchingFrameCues)
	{
		QueuePendingFrameChange(NewFrame);
		return;
	}

	{
		TGuardValue<bool> ReentryGuard(bDispatchingFrameCues, true);

		// Loop wrap = forward-playback frame index went backward.
		// Project does not use reverse playback (verified absent in plugin source);
		// if Reverse() / SetPlayRate(-1) is added later, revisit this heuristic.
		const bool bLoopWrap = (PreviousFrameIndex != INDEX_NONE && NewFrame < PreviousFrameIndex);

		// A loop wrap is a fresh Cue State lifecycle; re-evaluate every previously gated-out range at
		// its next Begin edge.
		//
		// It is a fresh HIT lifecycle for the same reason. The dedup key is (attacker, victim,
		// MoveInstanceCounter, window), and the counter only advances on a FLIPBOOK change — so a
		// looping attack that never switches flipbooks would otherwise rebuild the identical key on
		// every pass and dedup away every hit after the first, permanently. Reopening the ledger here
		// makes one loop == one swing, matching the Cue State lifecycle above.
		//
		// Deliberately the ledger only: MoveInstanceCounter is NOT bumped, because
		// `Sequence == uint16(MoveInstanceCounter)` is the wire producer invariant and a loop is not a
		// new move instance to a client. The whiff bookkeeping is likewise per-move-instance, not
		// per-loop — a move that connected on any pass has not whiffed.
		if (bLoopWrap)
		{
			LatchedOutRangeCues.Empty();
			ResetHitDedupWindow();
		}

		RefreshCachedLocalFrameState(NewFrame);

		// Net context resolved ONCE for this frame change (TASK-57 U8): drives the root-motion /
		// sprite-offset authority gates, the apply-mode drift corrector + authority stale-detect, and
		// the Frame Cue dispatch gate below. Standalone in single-player => every gate passes => the
		// whole block stays behaviorally identical to pre-net code.
		const EPaper2DPlusNetContext NetCtx = ResolveNetContext();

		// Apply-mode drift correction / authority self-heal (KTD-19/20). Skipped during the apply warm
		// itself (which sets the position explicitly). O(1) early-out when nothing is published.
		if (!bApplyingReplicatedState)
		{
			MaybeCorrectReplicatedPlaybackDrift(NetCtx);
		}

		if (bAutoApplyRootMotion && HasCachedRootMotion())
		{
			// Root motion is authority-only (KTD-16): proxies advance the baseline but never apply the
			// world offset (movement replication moves them — applying here too would fight it).
			const bool bApplyRootMotionWorld = Paper2DPlusNetGating::ShouldApplyRootMotion(NetCtx);
			ApplyRootMotionForFrame(NewFrame, bLoopWrap, bApplyRootMotionWorld);
			if (!bApplyRootMotionWorld && !bWarnedRootMotionGatedToProxy)
			{
				bWarnedRootMotionGatedToProxy = true;
				UE_LOG(LogPaper2DPlus, Warning,
					TEXT("'%s': bAutoApplyRootMotion is gated to authority on a network proxy — baseline advanced, world offset skipped. Enable actor/CMC movement replication so proxies see motion (see authority-contract.md)."),
					*GetName());
			}
		}

		// ─── Sprite offset application ──────────────────────────────────
		if (CachedCombatData && CachedCombatData->FrameExtractionInfo.Num() > 0)
		{
			if (GetResolvedFlipbookComponent())
			{
				CommitSpriteOffset(ComputeFrameSpriteOffset(NewFrame), NetCtx);
			}
		}

		// Per-tick frame span (TASK-57 U4): record (prev, new] BEFORE the Frame Cue dispatch (NOT after)
		// so any authority gameplay invoked from a Cue receiver on the newly entered
		// attack frame sees the CURRENT span when it calls ValidateAndRegisterHit / GetCurrentHitWindowIndex
		// (Codex F197b — handlers were seeing the previous span). PreviousFrameIndex still advances below, so
		// the (prev, new] window is unchanged. A 1-frame active window crossed entirely inside one multi-frame
		// server tick (prev=3 -> new=6) must still validate; a loop wrap (new < prev) is recorded verbatim and
		// the span sampler treats a non-ascending span as "just the end frame".
		LastFrameSpanBegin = PreviousFrameIndex;
		LastFrameSpanEnd = NewFrame;
		const EPaper2DPlusFrameCueEvaluationMode EvaluationMode =
			bApplyingReplicatedState
				? EPaper2DPlusFrameCueEvaluationMode::RuntimeCatchUp
				: EPaper2DPlusFrameCueEvaluationMode::RuntimePlayback;
		RuntimeFrameCueContext = MakeRuntimeFrameCueContext(
			NewFrame,
			PreviousFrameIndex,
			bLoopWrap,
			NetCtx,
			EvaluationMode);

		// Dead-asset defensive drop: composed Cue pointers are owned by the Layer asset. If it
		// died, rebuild/drop the view and purge active Layer ranges by pointer before dispatch.
		const bool bComposedViewOrphaned =
			ComposedLayerFrameCues.Num() > 0
			&& !AppearanceDigestLayerAsset.IsValid();
		const bool bDigestAssetDiedWithActives =
			bHasAppearanceCombatDigest
			&& ActiveRangeCues.Num() > 0
			&& AppearanceDigestLayerAsset.IsStale();
		if (bComposedViewOrphaned || bDigestAssetDiedWithActives)
		{
			RecomposeAppearanceCombatFrames();
			WarmCurrentFrameCueEffects();
		}

		// ─── Frame cue dispatch ──────────────────────────────────────────
		const bool bHasBaseCues = CachedFrameEventData && CachedFrameEventData->FrameCues.Num() > 0;
		if (bHasBaseCues || ComposedLayerFrameCues.Num() > 0)
		{
			TArray<TObjectPtr<UPaper2DPlusCueBase>> Snapshot;
			if (bHasBaseCues)
			{
				Snapshot = CachedFrameEventData->FrameCues;
			}
			Snapshot.Append(ComposedLayerFrameCues);

			FPaper2DPlusFrameCueContext Context = RuntimeFrameCueContext;

			const UWorld* World = GetWorld();
			bool bDedicatedServer = World && World->GetNetMode() == NM_DedicatedServer;
#if !UE_BUILD_SHIPPING
			bDedicatedServer = DedicatedServerOverrideForTests.Get(bDedicatedServer);
#endif
			bool bOwnerLocallyControlled = false;
			if (World)
			{
				if (const APawn* OwnerPawn = Cast<APawn>(GetOwner()))
				{
					bOwnerLocallyControlled = OwnerPawn->IsLocallyControlled();
				}
			}

			Paper2DPlusFrameCues::DispatchFrameTransition(
				Snapshot,
				Context,
				ActiveRangeCues,
				[this, &Context, bDedicatedServer, bOwnerLocallyControlled](UPaper2DPlusCueBase& Cue)
				{
					// Catch-up rebuilds active ranges but never replays cues.
					if (Context.bIsCatchUp && !Cue.IsRangeCue())
					{
						return false;
					}
					if (Cue.IsRangeCue())
					{
						if (ActiveRangeCues.Contains(&Cue)) { return true; }
						if (LatchedOutRangeCues.Contains(&Cue)) { return false; }
						const bool bAllowed = Paper2DPlusNetGating::ShouldDispatchFrameCue(
							Cue.NetPolicy,
							Context.NetContext,
							bDedicatedServer,
							bOwnerLocallyControlled,
							Context.bIsCatchUp);
						if (!bAllowed && Cue.ContainsFrame(Context.CurrentFrame))
						{
							LatchedOutRangeCues.Add(&Cue);
						}
						return bAllowed;
					}

					return Paper2DPlusNetGating::ShouldDispatchFrameCue(
						Cue.NetPolicy,
						Context.NetContext,
						bDedicatedServer,
						bOwnerLocallyControlled,
						Context.bIsCatchUp);
				},
				[this](UPaper2DPlusCueBase& Cue, const FPaper2DPlusFrameCueContext& CueContext)
				{
					NotifyFrameCue(Cue, CueContext);
				});
		}

		// ─── Automatic hit detection (TASK-145) ─────────────────────────
		// After Frame Cue dispatch so a window's Begin edge and first hits follow the frame's cues.
		// Arms off bCachedHasAttack (the frame-entry snapshot refreshed above — composed Layer tier
		// included), broadcasts the window edges, and runs one detection pass while armed. The
		// subsystem's armed-set tick re-checks BETWEEN key-frame transitions.
		UpdateAutoHitDetectionForFrame(NewFrame, NetCtx);

		PreviousFrameIndex = NewFrame;
		RuntimeFrameCueContext.CurrentFrame = NewFrame;
		RuntimeFrameCueContext.PreviousFrame = LastFrameSpanBegin;
		if (bPendingTerminalAllowsFinalFrame)
		{
			PendingFrameCueTerminalContext = RuntimeFrameCueContext;
			bPendingTerminalAllowsFinalFrame = false;
		}
	}

	FlushPendingAnimationDispatch();
}

void UPaper2DPlusCharacterProfileComponent::RefreshCachedLocalFrameState(int32 NewFrame)
{
	CachedLocalFrameData = FFrameHitboxData();
	CachedCurrentFrameIndex = NewFrame;
	CachedPivotFraction = FVector2D::ZeroVector;
	bCachedLocalFrameDataValid = false;
	bCachedHasAttack = false;
	bCachedHasHurtbox = false;
	MarkCachedWorldStateDirty();

	if (!CachedCombatData)
	{
		return;
	}

	// Composed indirection (layered-asset U5): a layered actor with an active composed tier reads the
	// component-owned composed frames; everyone else reads the raw cached view BYTE-IDENTICALLY. The
	// frame-level fields (bInvulnerable/DefenseClass) ride whichever copy — the compose seeds them from
	// the base by construction, so base always wins either way.
	const TArray<FFrameHitboxData>& EffectiveFrames =
		bComposedCombatActive ? ComposedCombatFrames : CachedCombatData->Frames;
	if (EffectiveFrames.IsValidIndex(NewFrame))
	{
		CachedLocalFrameData = EffectiveFrames[NewFrame];
	}

	if (UPaperFlipbook* Flipbook = CachedRuntimeFlipbook.Get())
	{
		FVector2D PivotLocal = FVector2D::ZeroVector;
		if (CharacterProfile && CharacterProfile->GetFramePivotLocal(Flipbook, NewFrame, PivotLocal))
		{
			CachedPivotFraction = Paper2DPlusFrameGeometry::ConvertFrameDataFromTopLeftToPivotSpace(
				CachedLocalFrameData,
				PivotLocal);
		}
	}

	bCachedHasAttack = CachedLocalFrameData.HasHitboxOfType(EHitboxType::Attack);
	bCachedHasHurtbox = CachedLocalFrameData.HasHitboxOfType(EHitboxType::Hurtbox);
	bCachedLocalFrameDataValid = true;
}

// ==========================================
// COMPOSED LAYER GAMEPLAY TIER
// ==========================================

void UPaper2DPlusCharacterProfileComponent::NotifyAppearanceCombatDirty(
	const FPaper2DPlusAppearanceDescriptor& CommittedAppearance,
	const UPaper2DPlusCharacterLayerAsset* LayerAsset)
{
#if !UE_BUILD_SHIPPING
	++AppearanceCombatPushCountForTests;
#endif

	// If the previous digest's asset already died, purge its active Layer Cue States by pointer before
	// overwriting the weak reference hides that staleness.
	PurgeDanglingComposedRangeCuesIfDigestAssetDied();
	if (!LayerAsset
		|| !Paper2DPlusAppearanceResolver::IsCompatible(CommittedAppearance, LayerAsset)
		|| !Paper2DPlusAppearanceResolver::AllowsGameplayComposition(CommittedAppearance.DeliveryMode))
	{
		ClearAppearanceCombatDigest();
		return;
	}

	// One descriptor is the authority. No persistent mirror maps survive beside it.
	AppearanceDigest = CommittedAppearance;
	Paper2DPlusAppearanceResolver::Normalize(AppearanceDigest);
	AppearanceDigestLayerAsset = LayerAsset;
	bHasAppearanceCombatDigest = true;

	// Re-entrancy (guard family, the bDispatchingFrameCues pattern): an equip from inside a BP
	// Frame Cue receiver (or a flipbook-change teardown handler) must not mutate the cached frame state
	// under the live dispatch — defer the recompose to FlushPendingAnimationDispatch (end of dispatch).
	if (bDispatchingFrameCues || bHandlingFlipbookChange)
	{
		bPendingAppearanceCombatDirty = true;
		return;
	}

	ApplyAppearanceCombatDigestNow();
}

void UPaper2DPlusCharacterProfileComponent::ClearAppearanceCombatDigest()
{
	// Never pushed => nothing composed, nothing to clear — non-layered actors (and layer components whose
	// profile sibling already tore down its tier) stay byte-identical: no digest is established, the
	// composed buffer never allocates, and no sweep/recompose runs.
	if (!bHasAppearanceCombatDigest)
	{
		return;
	}

	// If the digest's asset already died, purge its active Layer Cue States by pointer first.
	PurgeDanglingComposedRangeCuesIfDigestAssetDied();

	// Drop the digest (plain data writes — safe while dispatch is live, mirroring the push).
	AppearanceDigest = FPaper2DPlusAppearanceDescriptor();
	AppearanceDigestLayerAsset = nullptr;
	bHasAppearanceCombatDigest = false;

	// Re-entrancy (guard family): a teardown from inside a Frame Cue receiver / flipbook-change
	// teardown defers the recompose+sweep to FlushPendingAnimationDispatch — the SAME pending mechanism
	// as the push (the drain recomposes from the now-cleared digest, i.e. to empty, then sweeps).
	if (bDispatchingFrameCues || bHandlingFlipbookChange)
	{
		bPendingAppearanceCombatDirty = true;
		return;
	}

	// Recompose-to-empty + current-frame re-copy + the stale sweep: the sweep's union view is now base
	// events only, so every Layer-owned Cue State force-ends now — while the layer asset (still alive per
	// the caller's contract) keeps their objects valid.
	ApplyAppearanceCombatDigestNow();
}

void UPaper2DPlusCharacterProfileComponent::ApplyAppearanceCombatDigestNow()
{
	RecomposeAppearanceCombatFrames();
	WarmCurrentFrameCueEffects();

	// The dirty-notify tail (plan KTD): the equip changed what the CURRENT frame carries — re-copy the
	// current frame's local state (which also marks the lazy world tier dirty, so a world query THIS
	// frame re-derives against an unchanged transform — AE3) or, with no frame warmed yet, just dirty
	// the world tier.
	if (CachedCurrentFrameIndex != INDEX_NONE && CachedRuntimeFlipbook.IsValid())
	{
		RefreshCachedLocalFrameState(CachedCurrentFrameIndex);
	}
	else
	{
		MarkCachedWorldStateDirty();
	}

	// U6 (AE3, Cues half): this call is GUARANTEED outside Frame Cue dispatch (the pending mechanism
	// defers it to end-of-dispatch), so a removed Layer's in-flight Cue State force-ends immediately —
	// not on the next frame change.
	SweepStaleComposedRangeCuesNow();

	// U6: an equip can add/remove the Frame Cue feature mid-move — refresh the slow-poll tick state
	// (no-op on the event-driven path; OnFlipbookChanged already does this on flipbook changes).
	UpdateTickState();
}

void UPaper2DPlusCharacterProfileComponent::RecomposeAppearanceCombatFrames()
{
	bComposedCombatActive = false;
	ComposedLayerFrameCues.Reset();

	if (!bHasAppearanceCombatDigest || !CachedCombatData)
	{
		ComposedCombatFrames.Reset();
		return;
	}
	const UPaper2DPlusCharacterLayerAsset* LayerAsset = AppearanceDigestLayerAsset.Get();
	if (!LayerAsset)
	{
		ComposedCombatFrames.Reset();
		// Finding 1b: a DIGEST ASSET DEATH (IsStale — not an explicitly-pushed null) means any composed
		// active Layer Cue States died with it — drop them by pointer before anything sweeps.
	PurgeDanglingComposedRangeCuesIfDigestAssetDied();
		return;
	}

	// Resolve the FFlipbookProfileEntry* for the current cached move. The new ComposeCombatFrames
	// signature takes this instead of the raw FFlipbookCombatData& so it can access the full entry
	// from the selected Layers. Falls back to null gracefully:
	// ComposeCombatFrames returns false on a null Base, keeping the raw cached path.
	const FFlipbookProfileEntry* BaseEntry = (CharacterProfile && CachedRuntimeFlipbook.IsValid())
		? CharacterProfile->FindByFlipbookPtr(CachedRuntimeFlipbook.Get())
		: nullptr;
	bComposedCombatActive = Paper2DPlusLayerCombat::ComposeCombatFrames(
		BaseEntry,
		LayerAsset,
		AppearanceDigest,
		CachedMoveNameForCompose,
		ComposedCombatFrames);
	if (!bComposedCombatActive)
	{
		ComposedCombatFrames.Reset();
	}

	Paper2DPlusLayerCombat::ComposeFrameCues(
		BaseEntry,
		LayerAsset,
		AppearanceDigest,
		CachedMoveNameForCompose,
		ComposedLayerFrameCues);

	// A Layer can put Frame Cues on an actor whose base profile has none. This latch records that we
	// have SEEN it happen — but it is only a backstop, never the primary answer: it flips on an
	// OBSERVATION, and an observation is one animation late in exactly the way the poll rate is. The
	// rate itself comes from AppearanceDigestCarriesAnyFrameCues, which reads the Layer ASSET at push
	// time and therefore knows about a Layer cue on an animation that has never yet played.
	bObservedComposedLayerCues = bObservedComposedLayerCues || ComposedLayerFrameCues.Num() > 0;
}

void UPaper2DPlusCharacterProfileComponent::WarmCurrentFrameCueEffects()
{
	WarmedFrameCueEffectFlipbooks.Reset();
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	// Class-agnostic on purpose: each placement structurally reports its soft flipbook properties, so
	// every Cue Type is warmed on the same cooked rule and this seam never learns a cue class by name.
	TArray<TSoftObjectPtr<UPaperFlipbook>> DeclaredEffectArt;
	const auto WarmAuthoredCues =
		[this, &DeclaredEffectArt](const TArray<TObjectPtr<UPaper2DPlusCueBase>>& Cues)
	{
		for (const TObjectPtr<UPaper2DPlusCueBase>& CuePtr : Cues)
		{
			const UPaper2DPlusCueBase* Cue = CuePtr.Get();
			if (!Cue)
			{
				continue;
			}

			DeclaredEffectArt.Reset();
			Cue->CollectWarmableEffectArt(DeclaredEffectArt);
			for (const TSoftObjectPtr<UPaperFlipbook>& Reference : DeclaredEffectArt)
			{
				// Load now, hold a strong reference for as long as this animation is the current one:
				// that is the whole no-hitch contract, and the release happens when it is replaced.
				if (UPaperFlipbook* Flipbook = Reference.LoadSynchronous())
				{
					WarmedFrameCueEffectFlipbooks.AddUnique(Flipbook);
				}
			}
		}
	};

	if (CachedFrameEventData)
	{
		WarmAuthoredCues(CachedFrameEventData->FrameCues);
	}
	WarmAuthoredCues(ComposedLayerFrameCues);
}

void UPaper2DPlusCharacterProfileComponent::WarmDirectionalVariantArt(
	const FFlipbookProfileEntry& Entry)
{
	ResetDirectionalVariantWarm();
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	// Async on purpose: directional variants are cosmetic alternates a facing change may never
	// reach, so the warm must not trade the resolver's first-facing hitch for an animation-change
	// hitch. The streamable handle keeps every loaded variant resident while this animation is
	// current; ResetDirectionalVariantWarm releases them on the next animation change.
	TArray<FSoftObjectPath> VariantPaths;
	for (const FPaper2DPlusDirectionalAnimationSlot& Slot :
		Entry.DirectionalAnimationData.Slots)
	{
		if (!Slot.Flipbook.IsNull())
		{
			VariantPaths.AddUnique(Slot.Flipbook.ToSoftObjectPath());
		}
	}
	if (VariantPaths.IsEmpty())
	{
		return;
	}
	DirectionalVariantWarmHandle =
		UAssetManager::GetStreamableManager().RequestAsyncLoad(MoveTemp(VariantPaths));
}

void UPaper2DPlusCharacterProfileComponent::ResetDirectionalVariantWarm()
{
	if (!DirectionalVariantWarmHandle.IsValid())
	{
		return;
	}
	if (!DirectionalVariantWarmHandle->HasLoadCompleted())
	{
		DirectionalVariantWarmHandle->CancelHandle();
	}
	DirectionalVariantWarmHandle->ReleaseHandle();
	DirectionalVariantWarmHandle.Reset();
}

void UPaper2DPlusCharacterProfileComponent::NotifyFrameCue(
	UPaper2DPlusCueBase& Cue,
	const FPaper2DPlusFrameCueContext& CueContext)
{
	// The cue acts, then the world reacts. Behavior runs on the shared asset-owned placement, so it
	// must stay stateless; anything that mutates playback from here coalesces through the same
	// pending-dispatch machinery listeners already use, because both run inside the dispatch guard.
	Paper2DPlusFrameCueBehavior::ExecuteCueBehavior(Cue, CueContext);
	OnFrameCue.Broadcast(&Cue, CueContext);
}

void UPaper2DPlusCharacterProfileComponent::ForceEndActiveRangeCuesNow(
	const EPaper2DPlusFrameCueEndReason EndReason,
	const int32 CurrentFrameForContext)
{
	// If the digest's layer asset died since the last recompose, discard any Layer-owned active
	// Cue States by pointer before attempting lifecycle teardown.
	PurgeDanglingComposedRangeCuesIfDigestAssetDied();

	if (ActiveRangeCues.Num() > 0)
	{
		// Guard like a dispatch (the bDispatchingFrameCues family): these Ends run cue behavior and the
		// listener broadcast, so a receiver that switches animation or re-equips from inside one must
		// coalesce through the pending machinery instead of re-entering a teardown that is midway
		// through ActiveRangeCues. No drain happens here on purpose — the animation boundary already
		// holds bHandlingFlipbookChange and flushes once it is finished, and component teardown
		// deliberately discards whatever a dying receiver queues.
		TGuardValue<bool> ReentryGuard(bDispatchingFrameCues, true);

		TArray<TObjectPtr<UPaper2DPlusCueBase>> CueOrder;
		if (CachedFrameEventData)
		{
			CueOrder = CachedFrameEventData->FrameCues;
		}
		// Same orphan rule as ClaimFrameCueTerminal: a composed view whose digest asset died may point
		// at freed objects; ForceEndActiveRanges dereferences its order entries, so the dead view stays
		// out (its actives were already purged by pointer above).
		if (ComposedLayerFrameCues.Num() == 0 || AppearanceDigestLayerAsset.IsValid())
		{
			CueOrder.Append(ComposedLayerFrameCues);
		}

		FPaper2DPlusFrameCueContext Context = RuntimeFrameCueContext;
		Context.CurrentFrame = CurrentFrameForContext != INDEX_NONE
			? CurrentFrameForContext
			: RuntimeFrameCueContext.CurrentFrame;
		Context.PreviousFrame = RuntimeFrameCueContext.PreviousFrame;
		Context.NetContext = ResolveNetContext();
		Paper2DPlusFrameCues::ForceEndActiveRanges(
			CueOrder,
			Context,
			EndReason,
			ActiveRangeCues,
			[this](UPaper2DPlusCueBase& Cue, const FPaper2DPlusFrameCueContext& CueContext)
			{
				NotifyFrameCue(Cue, CueContext);
			});
	}
	LatchedOutRangeCues.Empty();
}

void UPaper2DPlusCharacterProfileComponent::SweepStaleComposedRangeCuesNow()
{
	if (ActiveRangeCues.Num() == 0)
	{
		return;
	}

	{
		// Guard like a dispatch: a receiver that equips again defers through the pending digest path.
		TGuardValue<bool> ReentryGuard(bDispatchingFrameCues, true);
		TArray<TObjectPtr<UPaper2DPlusCueBase>> CueUnionView;
		if (CachedFrameEventData)
		{
			CueUnionView = CachedFrameEventData->FrameCues;
		}
		CueUnionView.Append(ComposedLayerFrameCues);

		FPaper2DPlusFrameCueContext CueContext = RuntimeFrameCueContext;
		CueContext.CurrentFrame = CachedCurrentFrameIndex;
		CueContext.PreviousFrame = PreviousFrameIndex;
		CueContext.NetContext = ResolveNetContext();
		Paper2DPlusFrameCues::DispatchFrameTransition(
			CueUnionView,
			CueContext,
			ActiveRangeCues,
			[](UPaper2DPlusCueBase&) { return false; },
			[this](UPaper2DPlusCueBase& Cue, const FPaper2DPlusFrameCueContext& Context)
			{
				NotifyFrameCue(Cue, Context);
			});
	}

	// Drain anything a force-end handler queued (equips/flipbook changes deferred by the guard above).
	FlushPendingAnimationDispatch();
}

void UPaper2DPlusCharacterProfileComponent::PurgeDanglingComposedRangeCuesIfDigestAssetDied()
{
	// Only when a digest EXISTS, its asset genuinely DIED (IsStale — collected or marked garbage; a weak
	// ref explicitly set to null, or never set, reads false), and there is anything to purge. The alive
	// and null-pushed cases keep the normal sweep path, which force-ends properly.
	if (!bHasAppearanceCombatDigest
		|| ActiveRangeCues.Num() == 0
		|| !AppearanceDigestLayerAsset.IsStale())
	{
		return;
	}

	const TArray<TObjectPtr<UPaper2DPlusCueBase>>* BaseCues =
		CachedFrameEventData ? &CachedFrameEventData->FrameCues : nullptr;
	int32 NumCuesPurged = 0;
	for (auto It = ActiveRangeCues.CreateIterator(); It; ++It)
	{
		if (!BaseCues || !BaseCues->Contains(*It))
		{
			It.RemoveCurrent();
			++NumCuesPurged;
		}
	}
	if (NumCuesPurged > 0)
	{
		UE_LOG(LogPaper2DPlus, Verbose,
			TEXT("'%s': purged %d active Layer cue state(s) whose layer asset was garbage-collected."),
			*GetName(), NumCuesPurged);
	}
}

bool UPaper2DPlusCharacterProfileComponent::FrameHasAttackHitboxEffective(const FFlipbookProfileEntry* Entry, int32 KeyFrame) const
{
	// The composed tier is per-animation and belongs to the CURRENT cached move — apply it only when the
	// caller's entry IS that move (CachedCombatData aliases the entry's CombatData). Composed inputs are
	// the replicated committed appearance, so both net ends sample identically (no new net branch).
	if (bComposedCombatActive && Entry && CachedCombatData == &Entry->CombatData)
	{
		return KeyFrame >= 0
			&& ComposedCombatFrames.IsValidIndex(KeyFrame)
			&& ComposedCombatFrames[KeyFrame].HasHitboxOfType(EHitboxType::Attack);
	}
	return FrameHasAttackHitbox(Entry, KeyFrame);
}

void UPaper2DPlusCharacterProfileComponent::MarkCachedWorldStateDirty() const
{
	bCachedWorldStateValid = false;
	if (UWorld* World = GetWorld())
	{
		if (UPaper2DPlusHitboxSubsystem* HitboxSubsystem = World->GetSubsystem<UPaper2DPlusHitboxSubsystem>())
		{
			HitboxSubsystem->MarkIndexDirty();
		}
	}
}

bool UPaper2DPlusCharacterProfileComponent::RefreshCachedWorldState() const
{
	if (!bCachedLocalFrameDataValid)
	{
		return false;
	}

	UPaperFlipbookComponent* FBComp = GetResolvedFlipbookComponent();
	if (!FBComp)
	{
		return false;
	}

	const FTransform CurrentTransform = FBComp->GetComponentTransform();
	if (bCachedWorldStateValid && CachedWorldTransform.Equals(CurrentTransform))
	{
		return true;
	}

	CachedWorldTransform = CurrentTransform;
	CachedRuntimeFrameState = FPaper2DPlusRuntimeFrameState();
	CachedRuntimeFrameState.Flipbook = CachedRuntimeFlipbook.Get();
	CachedRuntimeFrameState.FrameIndex = CachedCurrentFrameIndex;
	CachedRuntimeFrameState.LocalFrameData = CachedLocalFrameData;

	// TASK-91: the facing/scale/location context reads through the ONE shared resolver so the
	// frame-parameterized attack-box build (BuildWorldAttackBoxesForFrame) can never diverge from it.
	ResolveWorldBoxTransformContext(*FBComp,
		CachedRuntimeFrameState.WorldPosition,
		CachedRuntimeFrameState.bFlipX,
		CachedRuntimeFrameState.ScaleX,
		CachedRuntimeFrameState.ScaleY);
	CachedRuntimeFrameState.WorldPosition = Paper2DPlusFrameGeometry::ApplyPivotFractionToWorldOrigin(
		CachedRuntimeFrameState.WorldPosition,
		CachedPivotFraction,
		CachedRuntimeFrameState.bFlipX,
		CachedRuntimeFrameState.ScaleX,
		CachedRuntimeFrameState.ScaleY);
	CachedRuntimeFrameState.bHasAttack = bCachedHasAttack;
	CachedRuntimeFrameState.bHasHurtbox = bCachedHasHurtbox;
	CachedRuntimeFrameState.bHasRootMotion = HasCachedRootMotion();
	CachedRuntimeFrameState.bHasFrameCues = HasCachedFrameCues();

	// TASK-77: the per-move default clash category an Attack box inherits when it has no category of its own.
	const FGameplayTag MoveDefaultClashCategory = CachedCombatData ? CachedCombatData->DefaultClashCategory : FGameplayTag();
	// TASK-77 U3: the FRAME-level defense class (Armor/Parry/Invincible) is threaded through the shared geometry
	// helper so runtime collision and editor previews stamp every emitted box identically.
	const FGameplayTag FrameDefenseClass = CachedLocalFrameData.DefenseClass;
	for (const FHitboxData& Hitbox : CachedLocalFrameData.Hitboxes)
	{
		FWorldHitbox WorldHitbox = Paper2DPlusFrameGeometry::MakeWorldHitbox(
			Hitbox,
			CachedRuntimeFrameState.WorldPosition,
			CachedRuntimeFrameState.bFlipX,
			CachedRuntimeFrameState.ScaleX,
			CachedRuntimeFrameState.ScaleY,
			MoveDefaultClashCategory,
			FrameDefenseClass);

		CachedRuntimeFrameState.WorldHitboxes.Add(WorldHitbox);
		if (Hitbox.Type == EHitboxType::Attack)
		{
			CachedRuntimeFrameState.WorldAttackBoxes.Add(WorldHitbox);
		}
		else if (Hitbox.Type == EHitboxType::Hurtbox)
		{
			CachedRuntimeFrameState.WorldHurtboxes.Add(WorldHitbox);
		}
	}

	for (const FSocketData& Socket : CachedLocalFrameData.Sockets)
	{
		CachedRuntimeFrameState.WorldSockets.Add(Paper2DPlusFrameGeometry::MakeWorldSocket(
			Socket,
			CachedRuntimeFrameState.WorldPosition,
			CachedRuntimeFrameState.bFlipX,
			CachedRuntimeFrameState.ScaleX,
			CachedRuntimeFrameState.ScaleY));
	}

	bCachedWorldStateValid = true;
	return true;
}

void UPaper2DPlusCharacterProfileComponent::ResolveWorldBoxTransformContext(
	const UPaperFlipbookComponent& FBComp, FVector& OutWorldPosition, bool& bOutFlipX, float& OutScaleX, float& OutScaleY)
{
	// The ONE facing/scale/location read shared by RefreshCachedWorldState and
	// BuildWorldAttackBoxesForFrame (TASK-91) — keep the delicate facing/scale rules in one place.
	OutWorldPosition = FBComp.GetComponentLocation();
	const FVector CompScale = FBComp.GetComponentScale();
	bOutFlipX = ResolveFacingLeft(CompScale, FBComp.GetComponentRotation().Yaw);
	OutScaleX = FMath::Max(FMath::Abs(CompScale.X), KINDA_SMALL_NUMBER);
	OutScaleY = FMath::Max(FMath::Abs(CompScale.Z), KINDA_SMALL_NUMBER);
}

bool UPaper2DPlusCharacterProfileComponent::BuildWorldAttackBoxesForFrame(
	const FFlipbookProfileEntry* Entry, int32 KeyFrame, TArray<FWorldHitbox>& OutBoxes) const
{
	// TASK-91 (Codex F197a): span-based hit validation resolves the LATEST in-span attack key frame,
	// which is often NOT the current frame when a coarse server tick crossed a 1-frame active window.
	// The cached world state only ever carries the CURRENT frame's boxes, so the validator needs this
	// frame-parameterized build to sample the resolved frame's geometry at the CURRENT world transform.
	OutBoxes.Reset();
	if (!Entry || KeyFrame < 0)
	{
		return false;
	}

	// Current-frame fast path: identical inputs — reuse the cached build (free, byte-identical).
	if (bCachedLocalFrameDataValid && KeyFrame == CachedCurrentFrameIndex && CachedCombatData == &Entry->CombatData)
	{
		return GetCachedWorldAttackBoxes(OutBoxes);
	}

	// Effective frame tier: the SAME rule as FrameHasAttackHitboxEffective / RefreshCachedLocalFrameState —
	// the composed Layer tier applies only when active AND Entry IS the current cached move.
	const bool bUseComposed = bComposedCombatActive && CachedCombatData == &Entry->CombatData;
	const TArray<FFrameHitboxData>& EffectiveFrames = bUseComposed ? ComposedCombatFrames : Entry->CombatData.Frames;
	if (!EffectiveFrames.IsValidIndex(KeyFrame))
	{
		return false;
	}

	UPaperFlipbookComponent* FBComp = GetResolvedFlipbookComponent();
	if (!FBComp)
	{
		return false;
	}

	// Copy: the pivot conversion mutates the frame data in place (top-left -> pivot space), exactly as
	// RefreshCachedLocalFrameState does for the current frame. Same pivot source (GetFramePivotLocal);
	// an unresolvable pivot leaves top-left space + zero fraction — matching the cached path's fallback.
	FFrameHitboxData FrameData = EffectiveFrames[KeyFrame];
	FVector2D PivotFraction = FVector2D::ZeroVector;
	if (UPaperFlipbook* Flipbook = CachedRuntimeFlipbook.Get())
	{
		FVector2D PivotLocal = FVector2D::ZeroVector;
		if (CharacterProfile && CharacterProfile->GetFramePivotLocal(Flipbook, KeyFrame, PivotLocal))
		{
			PivotFraction = Paper2DPlusFrameGeometry::ConvertFrameDataFromTopLeftToPivotSpace(FrameData, PivotLocal);
		}
	}

	FVector WorldPosition = FVector::ZeroVector;
	bool bFlipX = false;
	float ScaleX = 1.f;
	float ScaleY = 1.f;
	ResolveWorldBoxTransformContext(*FBComp, WorldPosition, bFlipX, ScaleX, ScaleY);
	WorldPosition = Paper2DPlusFrameGeometry::ApplyPivotFractionToWorldOrigin(
		WorldPosition, PivotFraction, bFlipX, ScaleX, ScaleY);

	// Same box emission as RefreshCachedWorldState, filtered to Attack (the validator's only need):
	// move default clash category + the frame's defense class thread through the shared geometry maker.
	const FGameplayTag MoveDefaultClashCategory = Entry->CombatData.DefaultClashCategory;
	const FGameplayTag FrameDefenseClass = FrameData.DefenseClass;
	for (const FHitboxData& Hitbox : FrameData.Hitboxes)
	{
		if (Hitbox.Type != EHitboxType::Attack)
		{
			continue;
		}
		OutBoxes.Add(Paper2DPlusFrameGeometry::MakeWorldHitbox(
			Hitbox, WorldPosition, bFlipX, ScaleX, ScaleY, MoveDefaultClashCategory, FrameDefenseClass));
	}
	return true;
}

bool UPaper2DPlusCharacterProfileComponent::GetCurrentRuntimeFrameState(FPaper2DPlusRuntimeFrameState& OutState) const
{
	if (!RefreshCachedWorldState())
	{
		OutState = FPaper2DPlusRuntimeFrameState();
		return false;
	}

	OutState = CachedRuntimeFrameState;
	return true;
}

bool UPaper2DPlusCharacterProfileComponent::TryGetCachedHitboxContext(FFrameHitboxData& OutFrameData, FVector& OutWorldPosition, bool& bOutFlipX, float& OutScaleX, float& OutScaleY) const
{
	if (!RefreshCachedWorldState())
	{
		return false;
	}

	OutFrameData = CachedRuntimeFrameState.LocalFrameData;
	OutWorldPosition = CachedRuntimeFrameState.WorldPosition;
	bOutFlipX = CachedRuntimeFrameState.bFlipX;
	OutScaleX = CachedRuntimeFrameState.ScaleX;
	OutScaleY = CachedRuntimeFrameState.ScaleY;
	return true;
}

bool UPaper2DPlusCharacterProfileComponent::GetCachedWorldHitboxes(TArray<FWorldHitbox>& OutHitboxes) const
{
	OutHitboxes.Empty();
	if (!RefreshCachedWorldState()) return false;
	OutHitboxes = CachedRuntimeFrameState.WorldHitboxes;
	return OutHitboxes.Num() > 0;
}

bool UPaper2DPlusCharacterProfileComponent::GetCachedWorldAttackBoxes(TArray<FWorldHitbox>& OutHitboxes) const
{
	OutHitboxes.Empty();
	if (!RefreshCachedWorldState()) return false;
	OutHitboxes = CachedRuntimeFrameState.WorldAttackBoxes;
	return OutHitboxes.Num() > 0;
}

bool UPaper2DPlusCharacterProfileComponent::GetCachedWorldHurtboxes(TArray<FWorldHitbox>& OutHitboxes) const
{
	OutHitboxes.Empty();
	if (!RefreshCachedWorldState()) return false;
	OutHitboxes = CachedRuntimeFrameState.WorldHurtboxes;
	return OutHitboxes.Num() > 0;
}

bool UPaper2DPlusCharacterProfileComponent::GetCachedWorldSockets(TArray<FWorldSocket>& OutSockets) const
{
	OutSockets.Empty();
	if (!RefreshCachedWorldState()) return false;
	OutSockets = CachedRuntimeFrameState.WorldSockets;
	return OutSockets.Num() > 0;
}

bool UPaper2DPlusCharacterProfileComponent::GetCachedWorldSocketByName(const FString& SocketName, FVector& OutLocation) const
{
	OutLocation = FVector::ZeroVector;
	if (!RefreshCachedWorldState()) return false;

	for (const FWorldSocket& Socket : CachedRuntimeFrameState.WorldSockets)
	{
		if (Socket.Name == SocketName)
		{
			OutLocation = Socket.Location;
			return true;
		}
	}
	return false;
}

void UPaper2DPlusCharacterProfileComponent::QueuePendingFlipbookChange(UPaperFlipbook* NewFlipbook)
{
	// Claim at request time, not drain time: stop→change keeps PlaybackStopped, while
	// change→stop fixes AnimationChanged before the later stop report arrives.
	ClaimFrameCueTerminal(
		EPaper2DPlusFrameCueEndReason::AnimationChanged,
		RuntimeFrameCueContext.PlaybackComponent,
		FrameCuePlaybackGeneration);
	PendingFlipbookChange = NewFlipbook;
	bHasPendingFlipbookChange = true;
	bHasPendingFrameChange = false;
	PendingFrameChange = INDEX_NONE;
}

void UPaper2DPlusCharacterProfileComponent::QueuePendingFrameChange(int32 NewFrame)
{
	if (bHasPendingFlipbookChange
		|| (bFrameCueTerminalAccepted && !bPendingTerminalAllowsFinalFrame)
		|| NewFrame == INDEX_NONE)
	{
		return;
	}

	PendingFrameChange = NewFrame;
	bHasPendingFrameChange = true;
}

void UPaper2DPlusCharacterProfileComponent::FlushPendingAnimationDispatch()
{
	if (bDispatchingFrameCues || bHandlingFlipbookChange)
	{
		return;
	}

	for (int32 Iteration = 0; Iteration < 8; ++Iteration)
	{
		if (bHasPendingFrameCueTerminal)
		{
			DrainPendingFrameCueTerminal();
			continue;
		}

		if (bHasPendingCharacterProfile)
		{
			UPaper2DPlusCharacterProfileAsset* PendingProfile =
				PendingCharacterProfile.Get();
			bHasPendingCharacterProfile = false;
			PendingCharacterProfile = nullptr;
			SetCharacterProfile(PendingProfile);
			continue;
		}

		if (bHasPendingFrameCuePlaybackSource)
		{
			UPaperFlipbookComponent* PendingSource =
				PendingFrameCuePlaybackSource.Get();
			bHasPendingFrameCuePlaybackSource = false;
			PendingFrameCuePlaybackSource = nullptr;
			ApplyFrameCuePlaybackSource(PendingSource);
			continue;
		}

		if (bHasPendingFlipbookChange)
		{
			UPaperFlipbook* PendingFlipbook = PendingFlipbookChange.Get();
			bHasPendingFlipbookChange = false;
			PendingFlipbookChange.Reset();
			HandleFlipbookChanged(PendingFlipbook);
			continue;
		}

		if (bHasPendingFrameChange)
		{
			const int32 PendingFrame = PendingFrameChange;
			bHasPendingFrameChange = false;
			PendingFrameChange = INDEX_NONE;
			HandleFrameChanged(PendingFrame);
			continue;
		}

		break;
	}

	// A NotifyAppearanceCombatDirty push that landed while
	// dispatch was live cached its digest and deferred here — apply it against the SETTLED state (any
	// queued flipbook change above already recomposed via OnFlipbookChanged; this drain is then an
	// idempotent re-apply that also refreshes the current frame's local copy).
	if (bPendingAppearanceCombatDirty)
	{
		bPendingAppearanceCombatDirty = false;
		ApplyAppearanceCombatDigestNow();
	}
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
	const float WorldZ = -PixelDelta.Y * FMath::Abs(CompScale.Z);

	return FVector(WorldX, 0.0f, WorldZ);
}

void UPaper2DPlusCharacterProfileComponent::ApplyRootMotionWorldDelta(const FVector& WorldDelta)
{
	if (WorldDelta.IsNearlyZero()) return;

	AActor* Owner = GetOwner();
	if (!Owner || !Owner->GetWorld())
	{
		return;
	}

	FHitResult Hit;
	switch (RootMotionApplicationMode)
	{
	case EPaper2DPlusRootMotionApplicationMode::DirectActorOffset:
		Owner->AddActorWorldOffset(WorldDelta, false, nullptr, ETeleportType::None);
		return;

	case EPaper2DPlusRootMotionApplicationMode::MovementComponent:
		if (UMovementComponent* MovementComponent = Owner->FindComponentByClass<UMovementComponent>())
		{
			if (USceneComponent* UpdatedComponent = MovementComponent->UpdatedComponent.Get())
			{
				MovementComponent->SafeMoveUpdatedComponent(
					WorldDelta,
					UpdatedComponent->GetComponentQuat(),
					true,
					Hit,
					ETeleportType::None);
				return;
			}
		}
		// Fall back to the collision-aware actor path when no movement component
		// owns an UpdatedComponent yet.
		Owner->AddActorWorldOffset(WorldDelta, true, &Hit, ETeleportType::None);
		return;

	case EPaper2DPlusRootMotionApplicationMode::SweptActorOffset:
	default:
		Owner->AddActorWorldOffset(WorldDelta, true, &Hit, ETeleportType::None);
		return;
	}
}

void UPaper2DPlusCharacterProfileComponent::ApplyRootMotionForFrame(int32 NewFrame, bool bLoopWrap, bool bApplyWorldDelta)
{
	if (!CachedMotionData || !CachedMotionData->RootMotion.IsValidIndex(NewFrame)) return;

	const FVector2D CurrentPos = CachedMotionData->RootMotion[NewFrame].Position;

	if (bLoopWrap)
	{
		// Seed the baseline to the wrap-to frame's position so the next
		// non-wrap frame computes delta against the correct reference.
		// When a large tick wraps directly to frame N, still apply frame 0 -> N
		// so the first part of the new loop is not silently lost.
		const FVector2D LoopBaseline = CachedMotionData->RootMotion.IsValidIndex(0)
			? CachedMotionData->RootMotion[0].Position
			: FVector2D::ZeroVector;
		LastAppliedRootMotionPos = LoopBaseline;

		if (NewFrame == 0 || CurrentPos.IsNearlyZero())
		{
			return;
		}

		const FVector2D PixelDelta = CurrentPos - LoopBaseline;
		LastAppliedRootMotionPos = CurrentPos;   // baseline advances IDENTICALLY in both modes
		const FVector WorldDelta = ComputeRootMotionWorldDelta(PixelDelta);
		if (WorldDelta.IsNearlyZero()) return;

		// bApplyWorldDelta=false (TASK-57 U8 proxy path): baseline advanced above, world offset skipped.
		if (bApplyWorldDelta)
		{
			ApplyRootMotionWorldDelta(WorldDelta);
			MarkCachedWorldStateDirty();
		}
		return;
	}

	if (CurrentPos.IsNearlyZero()) return;   // sparse: (0,0) frames are authoring-blank

	const FVector2D PixelDelta = CurrentPos - LastAppliedRootMotionPos;
	LastAppliedRootMotionPos = CurrentPos;   // baseline advances IDENTICALLY in both modes

	const FVector WorldDelta = ComputeRootMotionWorldDelta(PixelDelta);
	if (WorldDelta.IsNearlyZero()) return;

	if (bApplyWorldDelta)
	{
		ApplyRootMotionWorldDelta(WorldDelta);
		MarkCachedWorldStateDirty();
	}
}

bool UPaper2DPlusCharacterProfileComponent::NeedsTick() const
{
	if (bAutoApplyRootMotion && HasCachedRootMotion()) return true;
	if (HasCachedFrameCues()) return true;
	if (CachedCombatData && CachedCombatData->FrameExtractionInfo.Num() > 0) return true;
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

	// Poll fallback (stock UPaperFlipbookComponent users): keep watching even when the current
	// flipbook has no active features, otherwise an idle/no-feature animation can switch to an attack
	// with root motion/cues while this component's tick is disabled.
	if (NeedsTick())
	{
		PrimaryComponentTick.TickInterval = 0.0f;
		PrimaryComponentTick.SetTickFunctionEnable(true);
	}
	else if (CharacterProfile && GetResolvedFlipbookComponent())
	{
		// The RATE comes from what the PROFILE can dispatch, not from what the CURRENT animation
		// dispatches. The 20 Hz watch was sized for root motion, where a late sample costs 50 ms of
		// delay and nothing else. A Frame Cue anchor the sample never sees is not late — it is GONE:
		// an animation that starts and finishes inside one 50 ms window is never observed at all, so
		// none of its cues fire, ever. So an actor whose profile carries any cue is watched every
		// frame (the poll body is a pointer compare plus one key-frame lookup), and an actor whose
		// profile carries none keeps the cheap 20 Hz watch that policy was actually designed for.
		bool bWatchEveryFrame = ProfileCarriesAnyFrameCues();
#if !UE_BUILD_SHIPPING
		// The pre-fix policy, preserved as a test switch so the defect above stays reproducible.
		bWatchEveryFrame = bWatchEveryFrame && !bLegacySlowPollPolicyForTests;
#endif
		PrimaryComponentTick.TickInterval = bWatchEveryFrame ? 0.0f : SlowPollInterval;
		PrimaryComponentTick.SetTickFunctionEnable(true);
	}
	else
	{
		PrimaryComponentTick.SetTickFunctionEnable(false);
	}
}

bool UPaper2DPlusCharacterProfileComponent::ProfileCarriesAnyFrameCues() const
{
	const UPaper2DPlusCharacterProfileAsset* Profile = CharacterProfile;
	// Content revision, not just pointer identity: authoring the FIRST cue into an already-assigned
	// profile changes neither the pointer nor the element count, and this answer decides whether a
	// short animation's cues are sampled at all. Always 0 in cooked builds, so this is a free compare
	// there and the memo behaves exactly as it did before.
	const uint32 ProfileRevision = Profile ? Profile->GetEditorContentRevision() : 0;
	if (!bCueScanValid || CueScanProfileKey.Get() != Profile || CueScanProfileRevision != ProfileRevision)
	{
		bCueScanValid = true;
		CueScanProfileKey = Profile;
		CueScanProfileRevision = ProfileRevision;
		bCueScanFoundCues = false;
		if (Profile)
		{
			for (const FFlipbookProfileEntry& Entry : Profile->Flipbooks)
			{
				if (Entry.FrameEventData.FrameCues.Num() > 0)
				{
					bCueScanFoundCues = true;
					break;
				}
			}
		}
	}

	// Three sources, OR-ed, all of them answering "SOME animation", never "the current one":
	//  - the base profile's own placements;
	//  - what the equipped wardrobe Layer asset could add to ANY animation (the answer that makes a
	//    cue-free base profile plus a Layer-added cue safe — see the scan for why the latch alone was
	//    not enough);
	//  - the composed latch, kept as a backstop for anything the asset scan cannot see.
	return bCueScanFoundCues || AppearanceDigestCarriesAnyFrameCues() || bObservedComposedLayerCues;
}

bool UPaper2DPlusCharacterProfileComponent::AppearanceDigestCarriesAnyFrameCues() const
{
	// Only a LIVE digest can contribute: with no digest pushed (or its asset already dead) the
	// composed view is empty by construction and this must answer no, or every actor would over-tick.
	const UPaper2DPlusCharacterLayerAsset* LayerAsset =
		bHasAppearanceCombatDigest ? AppearanceDigestLayerAsset.Get() : nullptr;

	const FObjectKey LayerKey(LayerAsset);
	// Same reason as the base-profile scan: FObjectKey identifies the asset, not its contents, so a cue
	// authored into an already-equipped Layer must still re-open this question.
	const uint32 LayerRevision = LayerAsset ? LayerAsset->GetEditorContentRevision() : 0;
	if (!bCueScanLayerValid || CueScanLayerKey != LayerKey || CueScanLayerRevision != LayerRevision)
	{
		bCueScanLayerValid = true;
		CueScanLayerKey = LayerKey;
		CueScanLayerRevision = LayerRevision;
		bCueScanLayerFoundCues = false;

		// Mirror ComposeFrameCues' own gate: outside RuntimeCustomizable a Layer contributes no cues
		// at all (Fixed/Baked compile theirs into the Character Profile, where the base scan sees them).
		if (LayerAsset
			&& Paper2DPlusAppearanceResolver::AllowsGameplayComposition(LayerAsset->UsageMode))
		{
			// Deliberately every Layer and every animation, NOT the appearance-selected subset for the
			// current animation. The rate question is "could this actor ever dispatch a Layer cue",
			// and the composed set only ever describes the animation that is playing right now — so
			// asking it is exactly the mistake that put a short animation between two slow polls.
			// CookedGameplayAnimations is the same array the composer reads, so this cannot claim a
			// cue the composer would not produce; it can only claim one an unequipped Layer holds,
			// which over-ticks and never under-ticks.
			for (const FCharacterLayer& Layer : LayerAsset->Layers)
			{
				for (const FCharacterLayerAuthoredAnimationData& Animation : Layer.CookedGameplayAnimations)
				{
					if (Animation.FrameCues.Num() > 0)
					{
						bCueScanLayerFoundCues = true;
						break;
					}
				}
				if (bCueScanLayerFoundCues)
				{
					break;
				}
			}
		}
	}

	return bCueScanLayerFoundCues;
}

void UPaper2DPlusCharacterProfileComponent::EmitFrameCueDetectionDiagnostic(
	int32 DiagnosticId,
	const FString& Message)
{
	UE_LOG(LogPaper2DPlus, Warning, TEXT("%s"), *Message);

	// The id only distinguishes on-screen slots, which Shipping does not build.
	(void)DiagnosticId;

#if !UE_BUILD_SHIPPING
	DetectionDiagnosticsForTests.Add(Message);

	// A log line alone is the silence this exists to end: a designer watching a character fail to
	// fire a cue is looking at the viewport, not at the Output Log. The key is per actor AND per
	// diagnostic so two actors never overwrite each other's message.
	if (GEngine)
	{
		const uint64 ScreenKey =
			(static_cast<uint64>(GetUniqueID()) << 8) | static_cast<uint64>(DiagnosticId);
		GEngine->AddOnScreenDebugMessage(ScreenKey, 15.0f, FColor::Yellow, Message);
	}
#endif
}

void UPaper2DPlusCharacterProfileComponent::MaybeWarnDetectionPollTooCoarse(float RequestedPollIntervalSeconds)
{
	// Once per actor, and only for actors that actually have something to lose.
	if (bWarnedDetectionPollTooCoarse || !HasBegunPlay())
	{
		return;
	}

	// The claim is about the SAMPLING RATE this component asked for, never about one spiky DeltaTime.
	// An actor at TickInterval == 0 already watches every frame: there is no coarser-to-finer advice
	// left to give it, and a routine hitch (shader compile, sync asset load) that hands the tick a
	// 300 ms delta must not latch a permanent, undismissable on-screen alarm about a poll that missed
	// nothing. A false alarm nobody can clear teaches designers to ignore the real one — exactly the
	// silence R8 exists to end, arrived at from the other side.
	if (RequestedPollIntervalSeconds <= 0.0f)
	{
		return;
	}
	if (!HasCachedFrameCues())
	{
		return;
	}

	const UPaperFlipbook* Flipbook = CachedRuntimeFlipbook.Get();
	if (!Flipbook)
	{
		return;
	}

	// WALL-CLOCK life, not authored length: GetTotalDuration() is unscaled, so a 400 ms animation at
	// PlayRate 8 is over in 50 ms and steps between two samples exactly like a 50 ms one — while the
	// authored number (0.4 > 0.05) says the sample was plenty fine. Magnitude, so reverse playback is
	// judged on how long it lasts rather than on its sign; floored so the division is always defined,
	// which also makes a paused component (rate 0) effectively infinite and therefore never reported.
	const UPaperFlipbookComponent* ResolvedFlipbookComponent = GetResolvedFlipbookComponent();
	const float PlayRateMagnitude = ResolvedFlipbookComponent
		? FMath::Max(FMath::Abs(ResolvedFlipbookComponent->GetPlayRate()), KINDA_SMALL_NUMBER)
		: 1.0f;
	const float AuthoredDuration = Flipbook->GetTotalDuration();

	// An animation no longer than the interval that samples it can pass between two polls entirely.
	// This fires the moment such an animation is observed ONCE — an animation this component never
	// sees even once cannot be reported by anything, which is precisely why the rate above matters.
	const float EffectiveDuration = AuthoredDuration / PlayRateMagnitude;
	if (EffectiveDuration <= 0.0f || EffectiveDuration > RequestedPollIntervalSeconds)
	{
		return;
	}

	bWarnedDetectionPollTooCoarse = true;
	EmitFrameCueDetectionDiagnostic(
		/*DiagnosticId=*/1,
		FString::Printf(
			TEXT("Actor '%s': Frame Cue detection is polling '%s' every %.0f ms, but animation '%s' (profile '%s') lasts only %.0f ms at play rate %.2f — cues on animations this short can be missed entirely rather than fire late. Use UPaper2DPlusFlipbookComponent so flipbook and frame changes are pushed instead of sampled."),
			*GetNameSafe(GetOwner()),
			*GetNameSafe(ResolvedFlipbookComponent),
			RequestedPollIntervalSeconds * 1000.0f,
			*CachedMoveNameForCompose,
			*GetNameSafe(CharacterProfile),
			EffectiveDuration * 1000.0f,
			ResolvedFlipbookComponent ? ResolvedFlipbookComponent->GetPlayRate() : 1.0f));
}

void UPaper2DPlusCharacterProfileComponent::MaybeWarnUndispatchableFrameCuePlacements()
{
	if (bWarnedUndispatchableFrameCuePlacements || !HasBegunPlay())
	{
		return;
	}

	// EXACTLY the predicate dispatch uses, so this can never disagree with what actually happens:
	// a placement that fails it is skipped by DispatchFrameTransition without a word.
	int32 Undispatchable = 0;
	const auto CountUndispatchable =
		[&Undispatchable](const TArray<TObjectPtr<UPaper2DPlusCueBase>>& Cues)
	{
		for (const TObjectPtr<UPaper2DPlusCueBase>& Cue : Cues)
		{
			if (!Paper2DPlusFrameCueBehavior::IsPlacementResolvable(Cue))
			{
				++Undispatchable;
			}
		}
	};
	if (CachedFrameEventData)
	{
		CountUndispatchable(CachedFrameEventData->FrameCues);
	}
	CountUndispatchable(ComposedLayerFrameCues);

	if (Undispatchable == 0)
	{
		return;
	}

	bWarnedUndispatchableFrameCuePlacements = true;
	EmitFrameCueDetectionDiagnostic(
		/*DiagnosticId=*/2,
		FString::Printf(
			TEXT("Actor '%s' ('%s'): animation '%s' in profile '%s' carries %d Frame Cue placement(s) that dispatch cannot deliver — the placement is empty or its Cue Type asset was deleted, so those cues never fire. Character Profile validation lists them by index; fix or delete them in the Frame Cues tab."),
			*GetNameSafe(GetOwner()),
			*GetName(),
			*CachedMoveNameForCompose,
			*GetNameSafe(CharacterProfile),
			Undispatchable));
}

void UPaper2DPlusCharacterProfileComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Poll fallback only — event-driven path has tick disabled.
	// UpdateTickState() guards this, but belt-and-braces for the rare case where
	// tick is enabled before BeginPlay binds the delegates.
	if (bEventDrivenFlipbookDetection) return;

#if !UE_BUILD_SHIPPING
	++DetectionPollCountForTests;
#endif

	UPaperFlipbookComponent* const BoundSource = BoundFrameCuePlaybackSource.Get();

	// Re-assert the stock observer's finish listener before this poll can need it. OnFinishedPlaying
	// is the ENGINE's delegate and project code manages it (the stock unbind node, a Clear() when
	// re-arming a one-shot), so clearing the game's own handler used to take this one with it — and
	// this observer is the ONLY thing that distinguishes a natural finish from a stop on the stock
	// path, exactly as UPaper2DPlusFlipbookComponent's own latch is on the event-driven path.
	if (IsValid(BoundSource) && BoundStockPlaybackObserver)
	{
		BoundSource->OnFinishedPlaying.AddUniqueDynamic(
			BoundStockPlaybackObserver.Get(),
			&UPaper2DPlusFrameCueStockPlaybackObserver::HandleFinishedPlaying);
	}

	if (BoundSource && !IsValid(BoundSource))
	{
		ClaimFrameCueTerminal(
			EPaper2DPlusFrameCueEndReason::SourceRemoved,
			BoundSource,
			FrameCuePlaybackGeneration,
			/*bAllowFinalFrameBeforeDrain=*/false,
			/*bRequirePublicSourceMatch=*/false);
		DrainPendingFrameCueTerminal();
		FlushPendingAnimationDispatch();
		if (BoundFrameCuePlaybackSource.Get() == BoundSource)
		{
			ApplyFrameCuePlaybackSource(nullptr);
		}
		return;
	}

	UPaperFlipbookComponent* FBComp = GetResolvedFlipbookComponent();
	if (!FBComp) return;
	if (BoundFrameCuePlaybackSource != FBComp)
	{
		// Legacy direct C++ assignment after BeginPlay: fail closed on the old binding, then repair
		// through the supported atomic seam. Blueprint writes already route through the setter.
		SetFrameCuePlaybackSource(FBComp);
		return;
	}

	// Is the animation this poll is sampling short enough to slip between polls entirely? The question
	// is asked of the REQUESTED sampling period, not of DeltaTime: DeltaTime is one observed gap, and a
	// single frame hitch inflates it on an actor that is already watching every frame it can. Read here,
	// at tick entry, because HandleFlipbookChanged below re-runs UpdateTickState — which is precisely
	// the site that raises a cue-carrying actor to TickInterval == 0, and would otherwise erase the
	// coarse rate that this very poll was scheduled at before the diagnostic gets to see it.
	const float RequestedPollIntervalSeconds = PrimaryComponentTick.TickInterval;
	MaybeWarnDetectionPollTooCoarse(RequestedPollIntervalSeconds);

	UPaperFlipbook* CurrentFlipbook = FBComp->GetFlipbook();

	// Detect flipbook change → funnel through the unified handler
	// (which warms caches, dispatches frame-0 effects + root motion).
	if (CurrentFlipbook != PreviousFlipbook.Get())
	{
		HandleFlipbookChanged(CurrentFlipbook);
		// Re-check against the animation just warmed: the one the poll found late is the one most
		// likely to be too short for this sampling rate. Still the ENTRY-time interval — the warm
		// above has by now moved this actor to every-frame watching, which is the fix, not evidence
		// that the sample which found this animation late was fine.
		MaybeWarnDetectionPollTooCoarse(RequestedPollIntervalSeconds);
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

	// Terminal observation is deliberately last: Unreal latches OnFinishedPlaying before its final
	// cached frame, so stock compatibility must sample flipbook, then frame, then playing state.
	ProcessObservedFrameCuePlaybackState(FBComp);
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
	LastAppliedRootMotionPos = CurrentPos;   // advance the baseline regardless of authority

	// Non-authority networked callers get a ZERO delta (the baseline still advanced) so a proxy's
	// manual-drive movement integration can't fight movement replication (TASK-57 U8/KTD-16). Standalone
	// and Authority return the real delta unchanged.
	if (!Paper2DPlusNetGating::ShouldApplyRootMotion(ResolveNetContext()))
	{
		if (!bWarnedConsumeRootMotionOnProxy)
		{
			bWarnedConsumeRootMotionOnProxy = true;
			UE_LOG(LogPaper2DPlus, Warning,
				TEXT("'%s': ConsumeRootMotionDelta returns zero on a network proxy (baseline advanced) — root motion is authority-only. Drive proxy motion via movement replication (see authority-contract.md)."),
				*GetName());
		}
		return FVector::ZeroVector;
	}

	return ComputeRootMotionWorldDelta(PixelDelta);
}

bool UPaper2DPlusCharacterProfileComponent::ResolveFacingLeft(const FVector& ComponentScale, float YawDegrees)
{
	// THE single facing rule (hitboxes, root motion, replicated anim state, and the projectile-spawn
	// Frame Cue all resolve facing through here): |Yaw| in (90,270) OR negative X scale = facing left.
	// Normalize first — this is public + BlueprintPure, so a caller may pass an accumulated/raw yaw
	// (e.g. 540 == 180 == left); FRotator-sourced internal callers are already in [-180,180].
	const float AbsYaw = FMath::Abs(FRotator::NormalizeAxis(YawDegrees));
	return (AbsYaw > 90.0f && AbsYaw < 270.0f) || ComponentScale.X < 0.0f;
}

bool UPaper2DPlusCharacterProfileComponent::IsFacingLeft() const
{
	UPaperFlipbookComponent* FBComp = GetResolvedFlipbookComponent();
	if (!FBComp) return false;

	return ResolveFacingLeft(FBComp->GetComponentScale(), FBComp->GetComponentRotation().Yaw);
}

// ==========================================
// MOVE TRANSITIONS (TASK-76)
// ==========================================

const FFlipbookProfileEntry* UPaper2DPlusCharacterProfileComponent::ResolveCurrentMoveEntry(float* OutFramePosition) const
{
	// Header contract: OutFramePosition is reset to 0 on ANY null return — only the success path below
	// writes the real frame position.
	if (OutFramePosition)
	{
		*OutFramePosition = 0.f;
	}

	if (!CharacterProfile)
	{
		return nullptr;
	}

	UPaperFlipbookComponent* FBComp = GetResolvedFlipbookComponent();
	if (!IsValid(FBComp))
	{
		return nullptr;
	}

	UPaperFlipbook* Flipbook = FBComp->GetFlipbook();
	if (!Flipbook)
	{
		return nullptr;
	}

	const FFlipbookProfileEntry* Entry = CharacterProfile->FindByFlipbookPtr(Flipbook);
	if (!Entry)
	{
		return nullptr;
	}

	if (OutFramePosition)
	{
		// Current KEY-FRAME index as a float — the GetActorCurveValue house chain. CRITICAL:
		// GetKeyFrameIndexAtTime, NEVER GetPlaybackPositionInFrames (timeline frames are not
		// key-frame indices once FrameRun > 1 — see
		// docs/solutions/ue-paper2d-keyframe-vs-timeline-frame.md).
		const int32 KeyFrame = Flipbook->GetKeyFrameIndexAtTime(FBComp->GetPlaybackPosition());
		// INDEX_NONE = empty flipbook / negative position; clamp to 0 (flat-left-identical, fail-closed
		// for anchored curves), matching the sibling clamp sites.
		*OutFramePosition = KeyFrame == INDEX_NONE ? 0.f : static_cast<float>(KeyFrame);
	}

	return Entry;
}

FString UPaper2DPlusCharacterProfileComponent::GetCurrentMoveName() const
{
	if (!CharacterProfile)
	{
		return FString();
	}

	UPaperFlipbookComponent* FBComp = GetResolvedFlipbookComponent();
	if (!IsValid(FBComp))
	{
		return FString();
	}

	UPaperFlipbook* Flipbook = FBComp->GetFlipbook();
	if (!Flipbook)
	{
		return FString();
	}

	// The entry's authored FlipbookName is the move name; the UPaperFlipbook object name is only a
	// fallback for flipbooks the profile has no entry for (the GetActorCurveValue convention).
	const FFlipbookProfileEntry* Entry = CharacterProfile->FindByFlipbookPtr(Flipbook);
	return Entry ? Entry->Identity.FlipbookName : Flipbook->GetName();
}

UPaperFlipbook* UPaper2DPlusCharacterProfileComponent::GetCurrentMoveFlipbook() const
{
	// The current move IS the live flipbook — no name round-trip needed; this is the object form of
	// GetCurrentMoveName's resolution (GetResolvedFlipbookComponent -> GetFlipbook()).
	UPaperFlipbookComponent* FBComp = GetResolvedFlipbookComponent();
	return IsValid(FBComp) ? FBComp->GetFlipbook() : nullptr;
}

TArray<UPaper2DPlusCueBase*> UPaper2DPlusCharacterProfileComponent::GetActiveFrameCueRanges() const
{
	TArray<UPaper2DPlusCueBase*> Result;
	if (ActiveRangeCues.Num() == 0)
	{
		return Result;
	}
	Result.Reserve(ActiveRangeCues.Num());

	TSet<TObjectPtr<UPaper2DPlusCueBase>> Added;
	Added.Reserve(ActiveRangeCues.Num());
	const auto AppendAuthoredActive = [this, &Result, &Added](
		const TArray<TObjectPtr<UPaper2DPlusCueBase>>& AuthoredCues)
	{
		for (const TObjectPtr<UPaper2DPlusCueBase>& CuePtr : AuthoredCues)
		{
			UPaper2DPlusCueBase* Cue = CuePtr.Get();
			if (IsValid(Cue) && Cue->IsRangeCue() && ActiveRangeCues.Contains(Cue) && !Added.Contains(Cue))
			{
				Result.Add(Cue);
				Added.Add(Cue);
			}
		}
	};

	if (CachedFrameEventData)
	{
		AppendAuthoredActive(CachedFrameEventData->FrameCues);
	}
	AppendAuthoredActive(ComposedLayerFrameCues);

	// Under ordinary operation every active cue is still in one of the authored arrays above. During a
	// re-entrant source mutation there can briefly be a valid stale member awaiting the dispatch funnel's
	// forced End. Keep the query truthful and deterministic by appending those rare members by object path.
	TArray<TObjectPtr<UPaper2DPlusCueBase>> Remaining;
	for (const TObjectPtr<UPaper2DPlusCueBase>& CuePtr : ActiveRangeCues)
	{
		UPaper2DPlusCueBase* Cue = CuePtr.Get();
		if (IsValid(Cue) && Cue->IsRangeCue() && !Added.Contains(Cue))
		{
			Remaining.Add(Cue);
		}
	}
	Remaining.Sort([](const UPaper2DPlusCueBase& A, const UPaper2DPlusCueBase& B)
	{
		return A.GetPathName() < B.GetPathName();
	});
	for (UPaper2DPlusCueBase* Cue : Remaining)
	{
		Result.Add(Cue);
	}

	return Result;
}

bool UPaper2DPlusCharacterProfileComponent::NotifyOutcomeAuthorityGate(const TCHAR* WhichApi, bool& bWarnedLatch)
{
	// Standalone (incl. every worldless/single-player path) and Authority run the existing body — the
	// gate is a no-op there (C1 byte-identity). A networked non-authority context (AutonomousProxy /
	// SimulatedProxy) must not adjudicate/report: warn once, return false (caller no-ops).
	const EPaper2DPlusNetContext NetCtx = ResolveNetContext();
	if (NetCtx == EPaper2DPlusNetContext::AutonomousProxy || NetCtx == EPaper2DPlusNetContext::SimulatedProxy)
	{
		if (!bWarnedLatch)
		{
			bWarnedLatch = true;
			UE_LOG(LogPaper2DPlus, Warning,
				TEXT("%s called on a non-authority context on '%s' — ignored. The game reports/adjudicates combat ONLY on authority; the server validates hits and the outcome replicates down (TASK-57)."),
				WhichApi, *GetName());
		}
		return false;
	}
	return true;
}

// ==========================================
// SERVER-OWNED COMBAT (TASK-57 U4 / R3 / KTD-17)
// ==========================================

// The TASK-74 auxiliary curve multi-hit moves index off. A "HitWindow" Constant/step curve with keys
// e.g. (0,0)(4,1)(8,2) auto-segments a multi-hit move into windows 0/1/2 — the validator/dedup key
// floors the span-aware value so each window registers its own once-per-victim hit. Absent => window 0.
const FName UPaper2DPlusCharacterProfileComponent::HitWindowCurveName(TEXT("HitWindow"));

bool UPaper2DPlusCharacterProfileComponent::FrameHasAttackHitbox(const FFlipbookProfileEntry* Entry, int32 KeyFrame)
{
	if (!Entry || KeyFrame < 0 || !Entry->CombatData.Frames.IsValidIndex(KeyFrame))
	{
		return false;
	}
	return Entry->CombatData.Frames[KeyFrame].HasHitboxOfType(EHitboxType::Attack);
}

int32 UPaper2DPlusCharacterProfileComponent::ResolveLatestInSpanAttackFrame(const FFlipbookProfileEntry* Entry, bool bRequireActive) const
{
	if (!Entry)
	{
		return INDEX_NONE;
	}

	// The span recorded by HandleFrameChanged is (LastFrameSpanBegin, LastFrameSpanEnd]. Before any
	// frame change both are INDEX_NONE; fall back to the live key frame so a freshly-warmed move (the
	// frame-0 dispatch) still validates. A loop wrap (end < begin) collapses the span to just the end
	// frame (a wrapped span can't be walked ascending; the active window is at the end).
	int32 SpanEnd = LastFrameSpanEnd;
	if (SpanEnd == INDEX_NONE)
	{
		float LiveFramePos = 0.f;
		const FFlipbookProfileEntry* LiveEntry = ResolveCurrentMoveEntry(&LiveFramePos);
		SpanEnd = (LiveEntry == Entry) ? FMath::RoundToInt(LiveFramePos) : INDEX_NONE;
		if (SpanEnd == INDEX_NONE)
		{
			return INDEX_NONE;
		}
	}

	// SpanBegin is EXCLUSIVE; clamp to a contiguous ascending walk from (begin, end]. A non-ascending
	// span (loop wrap, or no prior frame) samples only the end frame.
	const int32 SpanBegin = (LastFrameSpanBegin != INDEX_NONE && LastFrameSpanBegin < SpanEnd)
		? LastFrameSpanBegin : (SpanEnd - 1);

	if (!bRequireActive)
	{
		// No attack-frame requirement: the span-end frame IS the sample (the latest frame this tick).
		return SpanEnd;
	}

	// Walk the span from the latest frame backward; the LATEST in-span attack key frame is the sample —
	// a 1-frame active window crossed entirely inside one multi-frame tick (prev=3 -> new=6) still
	// validates against the geometry at that attack frame. Reads the U5 composed tier when active (an
	// equipped weapon's Replace moves the attack frames; validation must agree with the world-box tier).
	for (int32 Frame = SpanEnd; Frame > SpanBegin; --Frame)
	{
		if (FrameHasAttackHitboxEffective(Entry, Frame))
		{
			return Frame;
		}
	}
	return INDEX_NONE;
}

void UPaper2DPlusCharacterProfileComponent::QueryAttackOverlapsForValidation(TArray<FHitboxCollisionResult>& OutResults,
	const FFlipbookProfileEntry* Entry, int32 SampleAttackFrame) const
{
	OutResults.Reset();

#if !UE_BUILD_SHIPPING
	// Worldless overlap-query seam (TASK-57 U4): the UWorldSubsystem is unreachable from worldless rigs,
	// so the injected results stand in for the server's own re-query. The real path is exercised by U7.
	if (OverlapQueryOverrideForTests)
	{
		OverlapQueryOverrideForTests(OutResults);
		return;
	}
#endif

	// Real path: re-query the world hitbox subsystem on the server so adjudication can't be trusted to
	// whatever frame a client happened to render.
	if (const UWorld* World = GetWorld())
	{
		if (UPaper2DPlusHitboxSubsystem* HitboxSubsystem = World->GetSubsystem<UPaper2DPlusHitboxSubsystem>())
		{
			// TASK-91 (Codex F197a): when the validator resolved an in-span attack frame OTHER than the
			// current frame (a coarse tick crossed a short active window and settled off it), the current
			// frame's cached boxes are the WRONG geometry — often empty, rejecting a hit the span check
			// was built to accept. Sweep the RESOLVED frame's boxes instead.
			if (Entry && SampleAttackFrame != INDEX_NONE && SampleAttackFrame != CachedCurrentFrameIndex)
			{
				TArray<FWorldHitbox> FrameAttackBoxes;
				if (BuildWorldAttackBoxesForFrame(Entry, SampleAttackFrame, FrameAttackBoxes))
				{
					HitboxSubsystem->QueryAttackOverlaps(
						const_cast<UPaper2DPlusCharacterProfileComponent*>(this), FrameAttackBoxes, OutResults);
					return;
				}
				// Build failure falls through to the current-frame query — the pre-fix behavior (fail-safe).
			}
			HitboxSubsystem->QueryAttackOverlaps(
				const_cast<UPaper2DPlusCharacterProfileComponent*>(this), OutResults);
		}
	}
}

int32 UPaper2DPlusCharacterProfileComponent::GetCurrentHitWindowIndex() const
{
	// Span-aware floor of the "HitWindow" aux curve at the LATEST in-span attack key frame — the SAME
	// sampler ValidateAndRegisterHit uses, so pick-window-then-validate can't straddle a tick boundary.
	// bRequireActive=false: the index is meaningful even off an attack frame (returns the end frame's
	// value), but the curve is read at the latest attack frame when one exists in the span.
	float Unused = 0.f;
	const FFlipbookProfileEntry* Entry = ResolveCurrentMoveEntry(&Unused);
	if (!Entry)
	{
		return 0; // no move context => single un-indexed window
	}

	const FPaper2DPlusFrameCurve* WindowCurve = Entry->CurveData.Curves.Find(HitWindowCurveName);
	if (!WindowCurve || !WindowCurve->HasKeys())
	{
		return 0; // absent/empty curve => single un-indexed window (the default)
	}

	// Prefer the latest in-span ATTACK frame (matching the validator); fall back to the span-end frame
	// when there's no attack frame in the span (so a mid-move query off an attack frame still reads the
	// authored window).
	int32 SampleFrame = ResolveLatestInSpanAttackFrame(Entry, /*bRequireActive=*/true);
	if (SampleFrame == INDEX_NONE)
	{
		SampleFrame = ResolveLatestInSpanAttackFrame(Entry, /*bRequireActive=*/false);
	}
	if (SampleFrame == INDEX_NONE)
	{
		SampleFrame = FMath::RoundToInt(Unused); // last resort: the live key frame
	}

	const float Value = WindowCurve->Eval(static_cast<float>(SampleFrame), 0.f);
	return FMath::Max(0, FMath::FloorToInt(Value));
}

void UPaper2DPlusCharacterProfileComponent::ResetHitDedupWindow()
{
	// Manual multi-hit seam (TASK-57 U4/KTD-17): clear the once-per-window ledger so the current move can
	// re-hit. Authority-agnostic on purpose — single-player dedup is a real feature; the counter +
	// ProcessedHits advance unconditionally (Standalone too).
	ProcessedHits.Reset();
}

bool UPaper2DPlusCharacterProfileComponent::RegisterHitOnce(AActor* Victim, int32 HitWindowIndex)
{
	// Non-authority gate (KTD-17): server-authoritative — a proxy/owner call registers nothing.
	if (!NotifyOutcomeAuthorityGate(TEXT("RegisterHitOnce"), bWarnedHitAdjudicationOnNonAuthority))
	{
		return false;
	}

	// Dedup-only (the LIGHT path): trusts the caller's own adjudication — no overlap re-query. Resolve
	// the window (AUTO = -1 => the span-aware "HitWindow" curve) and register the dedup tuple. A
	// duplicate returns false (no re-damage).
	const int32 ResolvedWindow = (HitWindowIndex < 0) ? GetCurrentHitWindowIndex() : HitWindowIndex;
	const FPaper2DPlusHitDedupKey Key(this, Victim, MoveInstanceCounter, ResolvedWindow);
	if (ProcessedHits.Contains(Key))
	{
		return false; // already registered this (attacker, victim, move-instance, window) tuple
	}
	ProcessedHits.Add(Key);
	bMoveInstanceRegisteredHit = true; // manual registrations count against the auto whiff (TASK-145)
	return true;
}

bool UPaper2DPlusCharacterProfileComponent::ValidateAndRegisterHit(
	AActor* Victim, FPaper2DPlusHitValidationResult& Out, int32 HitWindowIndex, bool bRequireActiveAttackFrame)
{
	Out = FPaper2DPlusHitValidationResult();

	// Non-authority gate (KTD-17): server-authoritative hit validation — a proxy/owner call warns once,
	// returns false, and registers NOTHING. Clients adjudicate cosmetically only (the BP library queries).
	if (!NotifyOutcomeAuthorityGate(TEXT("ValidateAndRegisterHit"), bWarnedHitAdjudicationOnNonAuthority))
	{
		return false;
	}

	// Reserved lag-comp seams populated regardless of outcome (the attacker's instance + server time).
	Out.MoveInstanceSeq = static_cast<int32>(MoveInstanceCounter);
	if (const TOptional<double> ServerNow = GetServerTimeSecondsForNet())
	{
		Out.ServerTimestamp = ServerNow.GetValue();
	}

	// Resolve the current move on the server's own state (key-frame indices only — GetKeyFrameIndexAtTime,
	// never GetPlaybackPositionInFrames; see docs/solutions/ue-paper2d-keyframe-vs-timeline-frame.md).
	float Unused = 0.f;
	const FFlipbookProfileEntry* Entry = ResolveCurrentMoveEntry(&Unused);
	if (Entry)
	{
		Out.MoveName = Entry->Identity.FlipbookName;
	}

	// Active-attack-frame check (span-based): when required, the move must be on an ATTACK key frame
	// within the last per-tick span (LastFrameSpanBegin, NewFrame]. A 1-frame active window crossed
	// inside one multi-frame tick validates; outside the span => no valid attack frame => false.
	int32 AttackFrame = INDEX_NONE;
	if (bRequireActiveAttackFrame)
	{
		AttackFrame = ResolveLatestInSpanAttackFrame(Entry, /*bRequireActive=*/true);
		if (AttackFrame == INDEX_NONE)
		{
			return false; // no in-span active attack frame — not a valid hit this tick
		}
	}
	else
	{
		// No active-frame requirement: still resolve a frame for the result + window curve sampling.
		AttackFrame = ResolveLatestInSpanAttackFrame(Entry, /*bRequireActive=*/false);
	}

	// Re-query the server's own overlap state (through the worldless seam in tests). VALID ⇔ a returned
	// overlap result resolves to the PASSED Victim's actor (the victim-membership contract, KTD-17).
	// TASK-91: the resolved span frame threads through so the real path samples THAT frame's attack
	// boxes when it differs from the current frame (the fast-tick lag-comp case, Codex F197a).
	TArray<FHitboxCollisionResult> Overlaps;
	QueryAttackOverlapsForValidation(Overlaps, Entry, AttackFrame);

	const FHitboxCollisionResult* Matched = nullptr;
	for (const FHitboxCollisionResult& Result : Overlaps)
	{
		// FHitboxCollisionResult::DefenderActor is the victim-actor field (the hurtbox owner). Membership
		// is an identity match on the PASSED victim — an overlap for a DIFFERENT victim never validates.
		// Result.bHit is the explicit confirmed-overlap flag: gate on it so bValidHit depends on the
		// result's own contract, not the subsystem's current invariant (today QueryAttackOverlaps only
		// emits bHit=true, but a future broadphase/miss candidate must never validate as a hit).
		if (Result.bHit && Result.DefenderActor == Victim && Victim != nullptr)
		{
			Matched = &Result;
			break;
		}
	}

	if (!Matched)
	{
		return false; // no server overlap resolved to the passed victim — bypass / phantom hit rejected
	}

	// Resolve the hit-window index (AUTO = -1 => the span-aware "HitWindow" curve, matching the sampler).
	const int32 ResolvedWindow = (HitWindowIndex < 0) ? GetCurrentHitWindowIndex() : HitWindowIndex;

	// Populate Out FROM the matched overlap — NEVER from caller input (KTD-17): damage/knockback/location
	// are the SERVER's authored data; the attacker key frame is the validated active frame.
	Out.bValidHit = true;
	Out.Victim = Matched->DefenderActor;
	Out.Damage = Matched->Damage;
	Out.Knockback = Matched->Knockback;
	Out.FrameIndex = AttackFrame;
	Out.HitWindowIndex = ResolvedWindow;

	// Dedup: once per (attacker, victim, move-instance, window). A duplicate is a REAL hit (bValidHit
	// stays true, fields populated so the caller can see what would have hit) that must NOT apply damage
	// twice — bDuplicate is set and the function returns false. The damage-apply gate is the bool return
	// (or bValidHit && !bDuplicate), NEVER bValidHit alone (matching the struct's documented semantics).
	const FPaper2DPlusHitDedupKey Key(this, Matched->DefenderActor, MoveInstanceCounter, ResolvedWindow);
	if (ProcessedHits.Contains(Key))
	{
		Out.bDuplicate = true;
		return false;
	}
	ProcessedHits.Add(Key);
	bMoveInstanceRegisteredHit = true; // manual registrations count against the auto whiff (TASK-145)
	return true;
}

// ==========================================
// AUTOMATIC HIT DETECTION (TASK-145)
// ==========================================

void UPaper2DPlusCharacterProfileComponent::UpdateAutoHitDetectionForFrame(int32 NewFrame, EPaper2DPlusNetContext NetCtx)
{
	// Fast path: feature off and nothing armed — zero cost for every non-auto actor.
	if (!bAutoHitDetection && !bAutoDetectArmed)
	{
		return;
	}

	// Armed = the entered frame carries attack boxes (bCachedHasAttack — the frame-entry snapshot,
	// composed Layer tier included) on an adjudicating context. Proxies never arm; their cosmetic
	// reactions ride Frame Cues (authority-contract.md).
	const bool bWantsArmed = bAutoHitDetection
		&& bCachedLocalFrameDataValid
		&& bCachedHasAttack
		&& Paper2DPlusNetGating::ShouldRunAutoHitDetection(NetCtx);

	if (bWantsArmed && !bAutoDetectArmed)
	{
		// Commit-before-broadcast (guard family): armed state + whiff bookkeeping are set before
		// receivers run. A receiver that switches flipbooks queues through the pending machinery —
		// this always runs under bDispatchingFrameCues.
		bAutoDetectArmed = true;
		bMoveInstanceHadArmedFrames = true;
		SetAutoDetectArmedRegistration(true);
		OnAttackWindowBegin.Broadcast(CachedMoveNameForCompose, NewFrame);
	}
	else if (!bWantsArmed && bAutoDetectArmed)
	{
		bAutoDetectArmed = false;
		SetAutoDetectArmedRegistration(false);
		OnAttackWindowEnd.Broadcast(CachedMoveNameForCompose, NewFrame);
	}

	if (bAutoDetectArmed)
	{
		RunAutoHitDetectionPass();
	}
}

void UPaper2DPlusCharacterProfileComponent::RunAutoHitDetectionPass()
{
	if (bRunningAutoHitPass)
	{
		return; // re-entry (a receiver re-triggering detection) is dropped, never recursed
	}
	TGuardValue<bool> ReentryGuard(bRunningAutoHitPass, true);

	// The same overlap funnel as ValidateAndRegisterHit (worldless seam included), against the
	// attacker's CURRENT cached world attack boxes.
	TArray<FHitboxCollisionResult> Overlaps;
	QueryAttackOverlapsForValidation(Overlaps);
	if (Overlaps.Num() == 0)
	{
		return;
	}

	// One window resolve per pass — every overlap in a single pass shares the span sample, exactly
	// like a ValidateAndRegisterHit call adjudicating the same tick.
	const int32 Window = GetCurrentHitWindowIndex();
	const int32 AttackFrame = CachedCurrentFrameIndex;
	const FString MoveName = CachedMoveNameForCompose;
	AActor* OwnerActor = GetOwner();

	// A receiver is free to destroy the attacker on its own hit (die-on-impact projectiles are the
	// common case). The remaining iterations would then keep mutating ProcessedHits on a pending-kill
	// component, so every broadcast below re-checks that we are still alive before continuing.
	const TWeakObjectPtr<UPaper2DPlusCharacterProfileComponent> WeakThis(this);

	for (const FHitboxCollisionResult& Overlap : Overlaps)
	{
		AActor* Victim = Overlap.DefenderActor;
		if (!Overlap.bHit || !IsValid(Victim))
		{
			continue;
		}

		// The SAME dedup ledger as ValidateAndRegisterHit/RegisterHitOnce: once per (attacker,
		// victim, move instance, hit window). The "HitWindow" curve segments multi-hit moves;
		// ResetHitDedupWindow reopens manually.
		const FPaper2DPlusHitDedupKey Key(this, Victim, MoveInstanceCounter, Window);
		if (ProcessedHits.Contains(Key))
		{
			continue;
		}
		ProcessedHits.Add(Key);
		bMoveInstanceRegisteredHit = true;

		FPaper2DPlusAutoHitResult Hit;
		Hit.Attacker = OwnerActor;
		Hit.AttackerComponent = this;
		Hit.Victim = Victim;
		Hit.VictimComponent = Victim->FindComponentByClass<UPaper2DPlusCharacterProfileComponent>();
		Hit.AttackBox = Overlap.AttackBox;
		Hit.HurtBox = Overlap.HurtBox;
		Hit.HitLocation = Overlap.HitLocation;
		Hit.Damage = Overlap.Damage;
		Hit.Knockback = Overlap.Knockback;
		Hit.MoveName = MoveName;
		Hit.FrameIndex = AttackFrame;
		Hit.HitWindowIndex = Window;

		// Commit-before-broadcast: the dedup entry above is already registered, so a receiver that
		// mutates playback/appearance can lose only a nested broadcast, never double-register.
		OnHitConnected.Broadcast(Hit);

		if (!WeakThis.IsValid())
		{
			return; // a receiver destroyed the attacker — stop touching our own state
		}

		if (IsValid(Hit.VictimComponent))
		{
			Hit.VictimComponent->OnHitReceived.Broadcast(Hit);

			if (!WeakThis.IsValid())
			{
				return; // the victim's receiver destroyed the attacker
			}
		}
	}
}

void UPaper2DPlusCharacterProfileComponent::TickAutoHitDetection()
{
	if (bDispatchingFrameCues || bHandlingFlipbookChange)
	{
		return; // a frame dispatch is live on this stack — it runs its own pass
	}
	if (!bAutoDetectArmed)
	{
		return;
	}

	const TWeakObjectPtr<UPaper2DPlusCharacterProfileComponent> WeakThis(this);

	{
		// Mirror HandleFrameChanged's re-entry protocol: receivers that switch flipbooks/frames
		// mid-broadcast queue through the same pending machinery and flush below.
		TGuardValue<bool> ReentryGuard(bDispatchingFrameCues, true);

		if (!bAutoHitDetection)
		{
			// The toggle was flipped off mid-window by a direct property write: disarm + close.
			bAutoDetectArmed = false;
			SetAutoDetectArmedRegistration(false);
			OnAttackWindowEnd.Broadcast(CachedMoveNameForCompose, CachedCurrentFrameIndex);
		}
		else
		{
			RunAutoHitDetectionPass();
		}
	}

	if (!WeakThis.IsValid())
	{
		return; // a receiver destroyed the attacker mid-pass — nothing left to flush
	}
	FlushPendingAnimationDispatch();
}

void UPaper2DPlusCharacterProfileComponent::FinalizeAutoHitDetectionForMoveEnd(bool bBroadcastWhiff)
{
	const bool bWasArmed = bAutoDetectArmed;
	const bool bWhiffed = bBroadcastWhiff && bMoveInstanceHadArmedFrames && !bMoveInstanceRegisteredHit;

	// Commit state before any broadcast (guard family): a receiver observing the end/whiff sees the
	// window closed and the flags reset for the incoming move.
	bAutoDetectArmed = false;
	bMoveInstanceHadArmedFrames = false;
	bMoveInstanceRegisteredHit = false;

	if (bWasArmed)
	{
		SetAutoDetectArmedRegistration(false);
		OnAttackWindowEnd.Broadcast(CachedMoveNameForCompose, CachedCurrentFrameIndex);
	}
	if (bWhiffed)
	{
		OnAttackWhiffed.Broadcast(CachedMoveNameForCompose);
	}
}

void UPaper2DPlusCharacterProfileComponent::SetAutoDetectArmedRegistration(bool bArmed)
{
	if (UWorld* World = GetWorld())
	{
		if (UPaper2DPlusHitboxSubsystem* HitboxSubsystem = World->GetSubsystem<UPaper2DPlusHitboxSubsystem>())
		{
			if (bArmed)
			{
				HitboxSubsystem->RegisterArmedAutoDetect(this);
			}
			else
			{
				HitboxSubsystem->UnregisterArmedAutoDetect(this);
			}
		}
	}
}

// ─── Networking seam (TASK-57 U1/U2) ────────────────────────────────────

EPaper2DPlusNetContext UPaper2DPlusCharacterProfileComponent::GetNetContext() const
{
	return ResolveNetContext();
}

EPaper2DPlusNetContext UPaper2DPlusCharacterProfileComponent::ResolveNetContext() const
{
#if !UE_BUILD_SHIPPING
	if (NetContextOverrideForTests.IsSet())
	{
		return NetContextOverrideForTests.GetValue();
	}
#endif

	// U2: real role mapping behind the bEnableReplication opt-in. The flag is CONSUMED at BeginPlay
	// (SetIsReplicated runs there) — once BeginPlay has run, the snapshot is what counts and a
	// runtime flip is inert (the component's net registration cannot retroactively change). The
	// flip is detected here, poll-free, on the entry points that consult the context — logged once.
	const bool bBegun = HasBegunPlay();
	if (bBegun && bEnableReplication != bReplicationEnabledAtBeginPlay && !bLoggedReplicationFlipPostBeginPlay)
	{
		bLoggedReplicationFlipPostBeginPlay = true;
		UE_LOG(LogPaper2DPlus, Warning,
			TEXT("bEnableReplication was flipped after BeginPlay on '%s' — the flip is INERT (the flag is consumed at BeginPlay; see the property tooltip). Set it on the archetype or before BeginPlay."),
			*GetName());
	}
	const bool bReplicationActive = bBegun ? bReplicationEnabledAtBeginPlay : bEnableReplication;
	if (!bReplicationActive)
	{
		return EPaper2DPlusNetContext::Standalone;
	}

	const UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Standalone)
	{
		return EPaper2DPlusNetContext::Standalone;
	}

	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return EPaper2DPlusNetContext::Standalone;
	}
	if (Owner->HasAuthority())
	{
		return EPaper2DPlusNetContext::Authority;
	}
	return Owner->GetLocalRole() == ROLE_AutonomousProxy
		? EPaper2DPlusNetContext::AutonomousProxy
		: EPaper2DPlusNetContext::SimulatedProxy;
}

void UPaper2DPlusCharacterProfileComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// UNCONDITIONAL registration (registration is not replication — SetIsReplicated gates traffic).
	// CharacterProfile pairs with RepProfileSeq in the same actor bunch (KTD-6); REPNOTIFY_Always
	// because a re-received pointer must still adopt the generation + drain the stash (KTD-7).
	DOREPLIFETIME_CONDITION_NOTIFY(UPaper2DPlusCharacterProfileComponent, CharacterProfile, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME(UPaper2DPlusCharacterProfileComponent, RepProfileSeq);
	DOREPLIFETIME(UPaper2DPlusCharacterProfileComponent, RepAnimState);
	DOREPLIFETIME(UPaper2DPlusCharacterProfileComponent, RepHitStop);
}

TOptional<double> UPaper2DPlusCharacterProfileComponent::GetServerTimeSecondsForNet() const
{
#if !UE_BUILD_SHIPPING
	if (ServerTimeOverrideForTests.IsSet())
	{
		return ServerTimeOverrideForTests;
	}
#endif

	if (const UWorld* World = GetWorld())
	{
		if (const AGameStateBase* GameState = World->GetGameState())
		{
			// Explicit double cast (KTD-21): the engine API returns float in 5.0 and double in 5.7.
			return TOptional<double>(static_cast<double>(GameState->GetServerWorldTimeSeconds()));
		}
	}
	// GameState unavailable (initial join, seamless travel, worldless) — callers stash or anchor -1.
	return TOptional<double>();
}

const FFlipbookProfileEntry* UPaper2DPlusCharacterProfileComponent::FindProfileEntryByMoveName(const FString& MoveName) const
{
	if (!CharacterProfile || MoveName.IsEmpty())
	{
		return nullptr;
	}
	for (const FFlipbookProfileEntry& Entry : CharacterProfile->Flipbooks)
	{
		if (Entry.Identity.FlipbookName.Equals(MoveName, ESearchCase::IgnoreCase))
		{
			return &Entry;
		}
	}
	return nullptr;
}

void UPaper2DPlusCharacterProfileComponent::PublishAnimStateIfAuthority()
{
	if (ResolveNetContext() != EPaper2DPlusNetContext::Authority)
	{
		return; // Standalone (incl. every worldless/single-player path) and proxies never publish.
	}

	// Producer contract (KTD-5 + the FPaper2DPlusRepAnimState::Sequence comment): the wire Sequence
	// IS uint16(MoveInstanceCounter) at every publish, with the sentinel-skip applied to the COUNTER
	// (not just the stamp) BEFORE stamping — 0 is the never-published sentinel and a raw cast would
	// eternally drop every 65,536th move instance on every client.
	if (static_cast<uint16>(MoveInstanceCounter) == 0)
	{
		++MoveInstanceCounter;
	}
	RepAnimState.Sequence = static_cast<uint16>(MoveInstanceCounter);
	RepAnimState.ProfileSeq = ProfileChangeCounter;

	UPaperFlipbookComponent* FBComp = GetResolvedFlipbookComponent();
	UPaperFlipbook* Flipbook = FBComp ? FBComp->GetFlipbook() : nullptr;
	const FFlipbookProfileEntry* Entry = (CharacterProfile && Flipbook) ? CharacterProfile->FindByFlipbookPtr(Flipbook) : nullptr;

	// A non-profile flipbook (locomotion — the game's/PaperZD's domain, A2) publishes an
	// authoritative "no active move" CLEAR so proxies are never wedged holding the last attack.
	RepAnimState.bIsProfileMove = (Entry != nullptr);
	RepAnimState.MoveName = Entry ? FName(*Entry->Identity.FlipbookName) : NAME_None;

	RepAnimState.PlayRate = FBComp ? FBComp->GetPlayRate() : 1.0f;
	RepAnimState.bLooping = FBComp ? FBComp->IsLooping() : false;
	RepAnimState.bReverse = FBComp ? FBComp->IsReversing() : false;
	// Facing rides the struct (KTD-4): hitbox mirroring and root-motion flip depend on it.
	RepAnimState.bFlipX = IsFacingLeft();

	// Server-clock anchor: ServerNow - CurrentPos/PlayRate. PlayRate <= 0 (paused) AND reverse
	// playback (IsReversing — mirrored into bReverse above) are both unrepresentable in the FORWARD
	// anchor model (a reversed position DECREASES with time) — publish anchor -1 and NEVER
	// divide/derive (the advisory degrades to position 0 / bAlreadyFinished=false); likewise when no
	// server clock exists yet (GameState null). U8's stale-detect re-anchors; full reverse-aware
	// derivation lands with U8's clock work.
	RepAnimState.StartServerTime = -1.0;
	if (FBComp && RepAnimState.PlayRate > 0.0f && !RepAnimState.bReverse)
	{
		const TOptional<double> ServerNow = GetServerTimeSecondsForNet();
		if (ServerNow.IsSet())
		{
			RepAnimState.StartServerTime =
				ServerNow.GetValue() - static_cast<double>(FBComp->GetPlaybackPosition()) / static_cast<double>(RepAnimState.PlayRate);
		}
	}
}

void UPaper2DPlusCharacterProfileComponent::WarnOnceOnRepWithoutLocalReplication(const TCHAR* OnRepName)
{
	if (!bEnableReplication && !bWarnedArchetypeMismatch)
	{
		bWarnedArchetypeMismatch = true;
		UE_LOG(LogPaper2DPlus, Warning,
			TEXT("%s: replicated Paper2DPlus data arrived on '%s' while bEnableReplication is false locally — the server and client archetypes disagree. bEnableReplication must match on both sides (TASK-57)."),
			OnRepName, *GetName());
	}
}

void UPaper2DPlusCharacterProfileComponent::OnRep_CharacterProfile(UPaper2DPlusCharacterProfileAsset* OldProfile)
{
	WarnOnceOnRepWithoutLocalReplication(TEXT("OnRep_CharacterProfile"));

	// Engine OnRep_SourceFlipbook stash-restore (KTD-7): the net driver pre-wrote the property, so a
	// naive SetCharacterProfile re-call would no-op against the identity early-out — restore the old
	// value first, then route the new one through THE existing funnel (single-cache-writer, C5).
	UPaper2DPlusCharacterProfileAsset* NewProfile = CharacterProfile;
	if (NewProfile != OldProfile)
	{
		CharacterProfile = OldProfile;
		TGuardValue<bool> ApplyGuard(bApplyingReplicatedState, true);
		SetCharacterProfile(NewProfile);
	}

	// Adopt the generation from the PAIRED RepProfileSeq — same-bunch atomicity (KTD-6). NEVER from
	// the anim struct (the adversarially-found spawn-order deadlock). NOTE: REPNOTIFY_Always governs
	// the RECEIVE side only — the net driver SEND-suppresses delta-equal properties, so a coalesced
	// A→B→A swap (or a late joiner whose archetype already equals the server's pointer) may ship
	// RepProfileSeq WITHOUT ever firing this OnRep. ApplyReplicatedAnimState carries the paired-
	// property adoption for that case; this site stays the primary adopter whenever the pointer
	// notify does fire.
	ProfileChangeCounter = RepProfileSeq;

	// Drain the stash: an anim state that arrived before (or with) this profile re-applies now.
	RetryPendingRepAnimState();
}

void UPaper2DPlusCharacterProfileComponent::OnRep_AnimState(const FPaper2DPlusRepAnimState& OldState)
{
	WarnOnceOnRepWithoutLocalReplication(TEXT("OnRep_AnimState"));
	ApplyReplicatedAnimState(RepAnimState);
}

void UPaper2DPlusCharacterProfileComponent::OnRep_HitStop()
{
	WarnOnceOnRepWithoutLocalReplication(TEXT("OnRep_HitStop"));

	// Opt-in gate (KTD-1): a component that doesn't replicate hit-stop ignores any snapshot that lands.
	if (!bReplicateHitStop)
	{
		return;
	}

	// OnReps NEVER run on the authority (the net driver only RepNotifies receivers) — no listen-server
	// double-freeze guard is needed; the authority's local freeze ran in BeginHitStopLocal already.
	const FPaper2DPlusRepHitStop& Snapshot = RepHitStop;

	// A never-published sentinel snapshot carries nothing (the authority bumps off 0 on the first
	// publish; a 0 here means a coalesced/empty default arrived) — clear the stash and ignore.
	if (Snapshot.HitStopSeq == 0)
	{
		PendingRepHitStop.Reset();
		return;
	}

	// Explicit CLEAR: Duration 0 (EndPlay / CancelHitStop / expiry publish) => end any local freeze
	// (ignore when none is active). No clock needed for a clear.
	if (Snapshot.DurationSeconds <= 0.f)
	{
		PendingRepHitStop.Reset();
		if (bHitStopActive)
		{
			// COMMIT-BEFORE-BROADCAST: EndHitStopInternal unwinds the freeze state, THEN broadcasts
			// OnHitStopEnd under bBroadcastingHitStop (the existing single end site; the proxy's own
			// re-anchor block is gated on Authority so it no-ops here).
			EndHitStopInternal();
		}
		return;
	}

	// Self-expiring clamp (KTD-21): Remaining = clamp(Duration - (ServerNow - StartServerTime), 0,
	// Duration), UNCONDITIONALLY — a seamless-travel / garbage clock can never freeze for an arbitrary
	// span. No server clock yet (GameState unreplicated — initial join / seamless travel) => STASH and
	// retry one frame (deferring is safer than applying the full Duration off a missing clock).
	const TOptional<double> ServerNow = GetServerTimeSecondsForNet();
	if (!ServerNow.IsSet())
	{
		PendingRepHitStop = Snapshot; // latest-wins
		// NON-ZERO InDelay (Codex-round-2 review): an InDelay of 0 fires WITHIN the same FTSTicker::Tick pass
		// (FireTime == CurrentTime; the Ticker.cpp:114 gate is strict), so a self-re-arming delay-0 ticker
		// that re-stashes while the clock is still missing loops forever inside one Tick() -> hang. A small
		// positive delay defers each retry to a LATER tick (breaks the loop, lets the network deliver the
		// clock between ticks). Matches the OnRep_AnimState re-arm fix.
		TWeakObjectPtr<UPaper2DPlusCharacterProfileComponent> WeakThis(this);
		FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateLambda([WeakThis](float /*DeltaTime*/)
			{
				if (UPaper2DPlusCharacterProfileComponent* Comp = WeakThis.Get())
				{
					Comp->RetryPendingRepHitStop();
				}
				return false; // one-shot
			}), /*InDelay=*/0.001f);
		return;
	}
	PendingRepHitStop.Reset();

	const double Elapsed = ServerNow.GetValue() - Snapshot.StartServerTime;
	const float Remaining = FMath::Clamp(
		Snapshot.DurationSeconds - static_cast<float>(Elapsed), 0.f, Snapshot.DurationSeconds);

	if (Remaining <= 0.f)
	{
		// A stale / late-join-expired snapshot: the freeze already elapsed. End a local freeze if one is
		// somehow active; otherwise ignore (no freeze to apply).
		if (bHitStopActive)
		{
			EndHitStopInternal();
		}
		return;
	}

	// NEW-vs-EXTEND from the WIRE seq, not local freeze state: a distinct HitStopSeq means a genuinely
	// fresh freeze (the authority bumps the seq only off bHitStopActive==false — i.e. the prior freeze
	// fully ended; an EXTEND keeps the seq). If a stale local freeze is still draining when a distinct-seq
	// snapshot lands (the prior freeze's clear coalesced away on the wire), REPLACE the remaining instead
	// of Max-extending it, so this proxy adopts the server's CURRENT freeze span. Update the tracker ONLY
	// here (the actual apply) — never on the no-clock stash — so RetryPendingRepHitStop re-derives the
	// same new-vs-extend verdict when it re-drives this path after the clock arrives.
	const bool bWireNewFreeze = (Snapshot.HitStopSeq != LastReceivedHitStopSeq);
	LastReceivedHitStopSeq = Snapshot.HitStopSeq;

	// Apply the (clamped) self-expiring freeze through the EXISTING refcounted registry + real-time
	// ticker — the SAME local primitive the authority uses. Late join mid-freeze gets the clamped
	// Remaining; a relevancy drop can never strand a frozen client because the local ticker self-expires
	// it. Null Victim tolerated (attacker-only freeze, no warn — KTD-24). bHitStopActive is set by
	// BeginHitStopLocal so the U8 drift corrector suppresses on this proxy (IsHitStopActive covers it).
	// Commit-before-broadcast is handled inside BeginHitStopLocal (state set, THEN OnHitStopBegin).
	BeginHitStopLocal(Remaining, Snapshot.Victim, /*bReplaceActiveFreeze=*/bWireNewFreeze);
}

void UPaper2DPlusCharacterProfileComponent::ApplyReplicatedAnimState(const FPaper2DPlusRepAnimState& State)
{
	if (State.Sequence == 0)
	{
		return; // Never-published sentinel — nothing to apply (KTD-5).
	}

	if (bBroadcastingReplicatedAnimState)
	{
		// Re-entered from inside its own advisory broadcast: committed state is already coherent
		// (commit-before-broadcast); only the nested apply/broadcast is dropped — the
		// suppress-or-coerce family rule. Verbose so automation needs no whitelist.
		UE_LOG(LogPaper2DPlus, Verbose,
			TEXT("ApplyReplicatedAnimState re-entered during its own advisory broadcast on '%s' — dropped."), *GetName());
		return;
	}

	// Paired-property generation adoption (TASK-57 U2 review BLOCKER fix): the net driver
	// SEND-suppresses delta-equal properties — a coalesced A→B→A profile swap, or a late joiner
	// whose archetype already equals the server's current pointer, ships RepProfileSeq WITHOUT
	// firing OnRep_CharacterProfile (REPNOTIFY_Always governs the RECEIVE side, not the send).
	// Property VALUES apply before notifies within a bunch, so RepProfileSeq is already current
	// here. Adopt ONLY when the locally-held profile pointer is non-null — KTD-6 preserved: the
	// generation is adopted from the PAIRED property, never the anim struct; a null pointer means
	// the profile may be unmapped/in-flight, so NO adoption => the stash row below defers and the
	// asset mapping re-runs OnRep_CharacterProfile, which adopts and drains.
	if (State.ProfileSeq == RepProfileSeq && CharacterProfile)
	{
		ProfileChangeCounter = RepProfileSeq;
	}

	// Defer/stash (decision-tree row B + KTD-6): BeginPlay pending (real worlds only — worldless
	// automation rigs never BeginPlay and must not stash forever), no server clock, profile-move
	// without a local profile, or generation mismatch (the paired profile OnRep has not landed — it
	// re-applies the stash after adoption). Latest-wins. The null-profile term is gated on
	// State.bIsProfileMove, and BOTH halves are deliberate (TASK-57 U2 review MAJOR fix): an
	// authoritative CLEAR (bIsProfileMove=false) needs no profile and must PASS THROUGH to the clear
	// path — a server running SetCharacterProfile(nullptr) mid-flipbook publishes a CLEAR that would
	// otherwise stash forever (no future OnRep against a null profile drains it); a profile-MOVE
	// state with a null profile still stashes (unmapped-GUID join safety — drained when the asset
	// mapping re-runs the profile OnRep). This row must stay AHEAD of the sequence/resolve checks
	// below so the null-profile stash wins before anything is committed against a profile we don't
	// hold.
	const bool bBeginPlayPending = (GetWorld() != nullptr) && !HasBegunPlay();
	const TOptional<double> ServerNow = GetServerTimeSecondsForNet();
	if (bBeginPlayPending || !ServerNow.IsSet() || (!CharacterProfile && State.bIsProfileMove)
		|| State.ProfileSeq != ProfileChangeCounter)
	{
		PendingRepAnimState = State;

		// Missing-clock re-arm (Codex F194a): the OTHER stash reasons each have their own drain — the
		// BeginPlay drain (RebroadcastReplicatedAnimState), and OnRep_CharacterProfile re-applying after
		// asset mapping. A missing server clock has NONE, so without this the stash stays stuck until some
		// unrelated replication happens (breaks late-join / seamless-travel recovery when anim-state arrives
		// before GameState). Schedule a one-shot core-ticker retry with a NON-ZERO InDelay (Codex-round-2
		// review). An InDelay of 0 sets FireTime == CurrentTime, which FTSTicker fires WITHIN THE SAME Tick
		// pass (Ticker.cpp do/while + PumpAddedElementsQueue; the FireTime > CurrentTime gate at
		// Ticker.cpp:114 is strict), so a self-re-arming delay-0 ticker that re-stashes while the clock is
		// still missing loops forever inside ONE Tick() -> hang/crash. A small positive delay defers each
		// retry to a LATER tick (breaks the loop AND lets the network deliver GameState between ticks).
		// RetryPendingRepAnimState is idempotent (peeks IsSet, clears, applies once); one retry is
		// outstanding at a time, self-cleaning on drain or component GC (WeakThis). Mirrors the OnRep_HitStop
		// re-arm (same fix applied there).
		if (!ServerNow.IsSet())
		{
			TWeakObjectPtr<UPaper2DPlusCharacterProfileComponent> WeakThis(this);
			FTSTicker::GetCoreTicker().AddTicker(
				FTickerDelegate::CreateLambda([WeakThis](float /*DeltaTime*/)
				{
					if (UPaper2DPlusCharacterProfileComponent* Comp = WeakThis.Get())
					{
						Comp->RetryPendingRepAnimState();
					}
					return false; // one-shot
				}), /*InDelay=*/0.001f);
		}
		return;
	}

	// Same sequence => nothing new to commit for the move itself; the only same-seq work left is the
	// re-anchor re-broadcast below (a same-Sequence republish carrying a CHANGED StartServerTime).
	if (State.Sequence == LastAppliedSequence)
	{
		PendingRepAnimState.Reset();

		// Same-seq RE-ANCHOR (Codex F195a): a same-Sequence republish carries a CHANGED StartServerTime when
		// the authority re-anchors mid-move (RepublishAnimState after SetPlaybackPosition/SetPlayRate, or the
		// U5 hit-stop clock re-anchor). The old code returned after only merging the outcome, so the new anchor
		// never reached anyone — advise-only consumers (the DEFAULT gate+advise mode) never re-fired the
		// advisory and the game's flipbook drifted; apply-mode proxies self-heal via the drift corrector EXCEPT
		// while hit-stop suppresses it. Detect the anchor change, re-derive the position, re-apply to an
		// apply-mode proxy, and re-broadcast the advisory. Outcome-only updates (anchor unchanged) skip this,
		// preserving the outcome-only fast path (no spurious advisory churn).
		if (State.bIsProfileMove && State.StartServerTime != LastAppliedStartServerTime)
		{
			LastAppliedStartServerTime = State.StartServerTime;
			const FFlipbookProfileEntry* ReEntry = FindProfileEntryByMoveName(State.MoveName.ToString());
			UPaperFlipbook* ReFlipbook = ReEntry ? ReEntry->Identity.Flipbook.Get() : nullptr;
			if (ReFlipbook)
			{
				float ReanchorPos = 0.0f;
				bool bReanchorFinished = false;
				if (State.StartServerTime >= 0.0)
				{
					ReanchorPos = Paper2DPlusNetGating::ComputeReplicatedPlaybackPosition(
						State.StartServerTime, ServerNow.GetValue(), State.PlayRate,
						ReFlipbook->GetTotalDuration(), State.bLooping, bReanchorFinished);
				}
				if (bApplyReplicatedAnimStateOnSimulatedProxies
					&& ResolveNetContext() == EPaper2DPlusNetContext::SimulatedProxy)
				{
					// SEEK-ONLY same-frame re-anchor (audit F6): a same-Sequence re-anchor (RepublishAnimState
					// after SetPlayRate, or the U5 hit-stop clock re-anchor) almost always lands on the SAME key
					// frame the proxy is already on. Routing every such re-anchor through
					// ApplyReplicatedFlipbookToProxy re-runs the full HandleFlipbookChanged catch-up warm, which
					// re-Ends + re-Begins every cosmetic ranged event (and bumps MoveInstanceCounter via
					// OnFlipbookChanged) on EVERY hit-stop unfreeze / republish. When the live flipbook already IS
					// this move AND the re-anchored key frame is unchanged, seek ONLY — honor the replicated play
					// state exactly like ApplyReplicatedFlipbookToProxy does, then SetPlaybackPosition without
					// firing events, so there is no ranged Begin/End churn and no move-instance bump. A genuine
					// frame jump still needs the full warm, so fall back to ApplyReplicatedFlipbookToProxy.
					UPaperFlipbookComponent* FBComp = GetResolvedFlipbookComponent();
					const int32 NewTarget = ReFlipbook->GetKeyFrameIndexAtTime(ReanchorPos);
					if (FBComp && FBComp->GetFlipbook() == ReFlipbook
						&& ReFlipbook->GetKeyFrameIndexAtTime(FBComp->GetPlaybackPosition()) == NewTarget)
					{
						FBComp->SetPlayRate(State.PlayRate);
						FBComp->SetLooping(State.bLooping);
						if (State.bReverse)
						{
							FBComp->ReverseFromEnd();
						}
						else
						{
							FBComp->SetPlaybackPosition(ReanchorPos, /*bFireEvents=*/false);
						}
					}
					else
					{
						ApplyReplicatedFlipbookToProxy(ReFlipbook, ReanchorPos, State);
					}
				}
				BroadcastReplicatedAnimStateAdvisory(State.MoveName, ReFlipbook, ReanchorPos, NAME_None, bReanchorFinished);
			}
		}
		return;
	}

	// New sequence, non-profile flipbook on the server => authoritative CLEAR: the game returns to
	// its own locomotion. Commit, THEN broadcast the null-move advisory.
	if (!State.bIsProfileMove)
	{
		LastAppliedSequence = State.Sequence;
		PendingRepAnimState.Reset();
		BroadcastReplicatedAnimStateAdvisory(NAME_None, nullptr, 0.0f, NAME_None, false);
		return;
	}

	// Resolve the move against the generation-matched profile.
	const FFlipbookProfileEntry* Entry = FindProfileEntryByMoveName(State.MoveName.ToString());
	UPaperFlipbook* Flipbook = Entry ? Entry->Identity.Flipbook.Get() : nullptr;
	if (Entry && !Flipbook)
	{
		Flipbook = Entry->Identity.Flipbook.LoadSynchronous();
	}
	if (!Flipbook)
	{
		// ProfileSeq MATCHED, so this is asset version skew (the client's profile genuinely lacks
		// the move) — warn + advisory CLEAR, NEVER a permanent stash (no future OnRep would drain
		// it — the security un-wedge rule). Commit the sequence so later same-seq outcome updates
		// take the outcome path instead of re-clearing.
		UE_LOG(LogPaper2DPlus, Warning,
			TEXT("OnRep_AnimState: move '%s' (seq %d) does not resolve on profile '%s' on '%s' — server/client asset version skew. Broadcasting an advisory CLEAR."),
			*State.MoveName.ToString(), static_cast<int32>(State.Sequence),
			CharacterProfile ? *CharacterProfile->GetName() : TEXT("null"), *GetName());
		LastAppliedSequence = State.Sequence;
		PendingRepAnimState.Reset();
		BroadcastReplicatedAnimStateAdvisory(NAME_None, nullptr, 0.0f, NAME_None, false);
		return;
	}

	// COMMIT EVERYTHING FIRST (the thrice-learned commit-before-broadcast rule): sequence + the
	// re-anchor baseline — THEN broadcast.
	LastAppliedSequence = State.Sequence;
	LastAppliedStartServerTime = State.StartServerTime; // baseline for the same-seq re-anchor detector (F195a)
	PendingRepAnimState.Reset();

	// Anchor-derived advisory position. Anchor -1 (paused/zero-rate publish, or a clockless publish
	// awaiting U8's re-anchor) => position 0 with bAlreadyFinished=false — documented on the delegate.
	float PlaybackPosition = 0.0f;
	bool bAlreadyFinished = false;
	if (State.StartServerTime >= 0.0)
	{
		PlaybackPosition = Paper2DPlusNetGating::ComputeReplicatedPlaybackPosition(
			State.StartServerTime, ServerNow.GetValue(), State.PlayRate,
			Flipbook->GetTotalDuration(), State.bLooping, bAlreadyFinished);
	}

	// APPLY MODE (TASK-57 U8): on a simulated proxy that opted in, the component PLAYS the move itself —
	// the second gate+advise exception. Order is commit (above) -> APPLY -> broadcast. Every other
	// context (the owner, or a proxy in advise-only mode) only broadcasts; the game plays the flipbook.
	if (bApplyReplicatedAnimStateOnSimulatedProxies
		&& ResolveNetContext() == EPaper2DPlusNetContext::SimulatedProxy)
	{
		ApplyReplicatedFlipbookToProxy(Flipbook, PlaybackPosition, State);
	}

	BroadcastReplicatedAnimStateAdvisory(State.MoveName, Flipbook, PlaybackPosition, NAME_None, bAlreadyFinished);
}

void UPaper2DPlusCharacterProfileComponent::ApplyReplicatedFlipbookToProxy(
	UPaperFlipbook* Flipbook, float PlaybackPosition, const FPaper2DPlusRepAnimState& State)
{
	if (!Flipbook)
	{
		return;
	}
	UPaperFlipbookComponent* FBComp = GetResolvedFlipbookComponent();
	if (!FBComp)
	{
		return;
	}

	// Catch-up target = the key frame at the anchored position, so the flipbook-change funnel warms to
	// this MID-MOVE frame (not frame 0) with bIsCatchUp=true (one-shots suppressed, ranged rebuilt).
	const int32 TargetFrame = Flipbook->GetKeyFrameIndexAtTime(PlaybackPosition);

	// bApplyingReplicatedState routes the funnel through the catch-up path (and bypasses the proxy gates
	// on the existing mutators); PendingCatchUpTargetFrame tells HandleFlipbookChanged which frame to
	// warm to. Both restore on scope exit so subsequent forward playback is normal (bIsCatchUp=false).
	TGuardValue<bool> ApplyGuard(bApplyingReplicatedState, true);
	TGuardValue<int32> CatchUpGuard(PendingCatchUpTargetFrame, TargetFrame);

	// Preserve a PAUSED (0) replicated play rate (Codex F195b): a paused server move publishes PlayRate=0
	// with no clock anchor; clamping it to 1.0 made the proxy play forward from frame 0 while the authority
	// stayed paused (and drift correction can't help — the anchor is -1). The publish side carries the real
	// rate (GetPlayRate), so the proxy honors it verbatim; SetPlayRate(0) pauses at the caught-up frame.
	FBComp->SetPlayRate(State.PlayRate);
	FBComp->SetLooping(State.bLooping);

	if (FBComp->GetFlipbook() != Flipbook)
	{
		// Switch through the funnel: on a UPaper2DPlusFlipbookComponent, SetFlipbook fires
		// OnFlipbookChanged -> HandleFlipbookChanged, which warms to PendingCatchUpTargetFrame under the
		// guards above. A STOCK UPaperFlipbookComponent (the slow-poll compatibility path, AND the
		// worldless test rigs) does NOT fire that delegate — only our subclass's SetFlipbook override
		// does — so the catch-up warm would never run and the mid-move ranged Begin would be lost.
		// Drive HandleFlipbookChanged explicitly in that case (still inside the bApplyingReplicatedState
		// + PendingCatchUpTargetFrame guards). For our subclass, SetFlipbook auto-fires it, so calling
		// it again would double-dispatch — call exactly one of the two paths.
		FBComp->SetFlipbook(Flipbook);
		if (Cast<UPaper2DPlusFlipbookComponent>(FBComp) == nullptr)
		{
			HandleFlipbookChanged(Flipbook);
		}
	}
	else
	{
		// Same flipbook (a same-flipbook NEW sequence — e.g. a self-loop restart): the engine SetFlipbook
		// identity early-out never fires the funnel, so drive the catch-up warm directly.
		HandleFlipbookChanged(Flipbook);
	}

	if (State.bReverse)
	{
		// Reversed publishes carry no anchor (position 0) — the forward anchor model cannot place a
		// reverse move, so play it backward from the end (approximate; documented limitation).
		FBComp->ReverseFromEnd();
	}
	else
	{
		FBComp->SetPlaybackPosition(PlaybackPosition, /*bFireEvents=*/false);
	}
}

void UPaper2DPlusCharacterProfileComponent::MaybeCorrectReplicatedPlaybackDrift(EPaper2DPlusNetContext NetCtx)
{
	// O(1) early-outs — zero single-player cost (nothing published in Standalone).
	if (RepAnimState.Sequence == 0 || RepAnimState.StartServerTime < 0.0)
	{
		return;
	}
	if (!RepAnimState.bIsProfileMove)
	{
		return;   // an authoritative CLEAR carries no clock to correct against
	}
	if (IsHitStopActive())
	{
		return;   // hit-stop legitimately pauses the move clock (the U5 re-anchor restores the anchor)
	}

	const bool bAuthority = (NetCtx == EPaper2DPlusNetContext::Authority);
	const bool bApplyProxy = (NetCtx == EPaper2DPlusNetContext::SimulatedProxy)
		&& bApplyReplicatedAnimStateOnSimulatedProxies;
	if (!bAuthority && !bApplyProxy)
	{
		return;   // advise-only proxies and the owner don't snap; the game owns playback
	}

	const TOptional<double> ServerNow = GetServerTimeSecondsForNet();
	if (!ServerNow.IsSet())
	{
		return;
	}
	UPaperFlipbookComponent* FBComp = GetResolvedFlipbookComponent();
	if (!FBComp)
	{
		return;
	}
	UPaperFlipbook* LiveFlipbook = FBComp->GetFlipbook();
	if (!LiveFlipbook)
	{
		return;
	}

	// Correct only while the live flipbook IS the published move (never across a move change — the
	// next publish/OnRep owns that transition). A directional variant aliases its canonical base row;
	// visual direction remains local while the replicated move identity stays base-only.
	const FFlipbookProfileEntry* Entry = FindProfileEntryByMoveName(RepAnimState.MoveName.ToString());
	bool bLiveOwnerAmbiguous = false;
	const FFlipbookProfileEntry* LiveOwner = CharacterProfile
		? CharacterProfile->ResolveLogicalAnimationOwner(
			LiveFlipbook,
			bLiveOwnerAmbiguous,
			EPaper2DPlusLogicalOwnerDuplicatePolicy::PreserveBaseOnlyIterationWinner)
		: nullptr;
	if (!Entry || bLiveOwnerAmbiguous || LiveOwner != Entry)
	{
		return;
	}

	bool bFinished = false;
	const float Length = LiveFlipbook->GetTotalDuration();
	const float Expected = Paper2DPlusNetGating::ComputeReplicatedPlaybackPosition(
		RepAnimState.StartServerTime, ServerNow.GetValue(), RepAnimState.PlayRate,
		Length, RepAnimState.bLooping, bFinished);
	const float Actual = FBComp->GetPlaybackPosition();

	// Circular drift on a looping move (review fix): the engine playback clock and the server-wall-clock
	// fmod wrap INDEPENDENTLY, so at a loop-wrap frame one may have crossed the period boundary while the
	// other has not — making the raw |Expected-Actual| ~= Length for a single frame. Measure the SHORTER
	// way around the loop so a wrap straddle reads as a SMALL distance, not a full period; otherwise every
	// loop wrap of every looping move spuriously re-anchors (authority wire churn) or back-snaps (proxy
	// hitch). Non-looping moves use the linear distance.
	float Drift = FMath::Abs(Expected - Actual);
	if (RepAnimState.bLooping && Length > KINDA_SMALL_NUMBER)
	{
		Drift = FMath::Min(Drift, Length - Drift);
	}

	if (bAuthority)
	{
		// Authority self-heal is a COARSE safety net for a game that mutated playback (rate/position)
		// without calling RepublishAnimState — a band WIDER than the proxy snap tolerance so a
		// continuously-mutating game cannot churn the wire every frame (RepublishAnimState is the precise,
		// immediate tool; KTD-19). Below the band, the linear-model error is tolerated.
		const float AuthorityReanchorBand = FMath::Max(NetPlaybackSnapToleranceSeconds * 4.0f, 0.25f);
		if (Drift > AuthorityReanchorBand)
		{
			PublishAnimStateIfAuthority();
		}
	}
	else if (Drift > NetPlaybackSnapToleranceSeconds)
	{
		// Apply-mode proxy: snap local playback to the server-formula position (tight, for visual sync;
		// no event fire — SetPlaybackPosition with bFireEvents=false does not re-enter HandleFrameChanged).
		FBComp->SetPlaybackPosition(Expected, /*bFireEvents=*/false);
	}
}

void UPaper2DPlusCharacterProfileComponent::RepublishAnimState()
{
	const EPaper2DPlusNetContext Ctx = ResolveNetContext();
	if (Ctx == EPaper2DPlusNetContext::Standalone)
	{
		// Single-player / not networked: a clean no-op (nothing is replicated). NEVER warn — this is a
		// documented BlueprintCallable that the most common (single-player Fab) consumer calls after a
		// playback mutation, and Standalone is not an error (review fix: Standalone != Authority but is
		// not a misuse).
		return;
	}
	if (Ctx != EPaper2DPlusNetContext::Authority)
	{
		if (!bWarnedRepublishOnNonAuthority)
		{
			bWarnedRepublishOnNonAuthority = true;
			UE_LOG(LogPaper2DPlus, Warning,
				TEXT("'%s': RepublishAnimState called on a network proxy — ignored. Only the server re-anchors replicated anim state."),
				*GetName());
		}
		return;
	}
	// Re-anchor to the current actual position + params, SAME Sequence — PublishAnimStateIfAuthority
	// stamps uint16(MoveInstanceCounter) without bumping the counter, so this is not a new move
	// instance (KTD-19). No-op when nothing is playing / no server clock.
	PublishAnimStateIfAuthority();
}

void UPaper2DPlusCharacterProfileComponent::RetryPendingRepAnimState()
{
	if (PendingRepAnimState.IsSet())
	{
		const FPaper2DPlusRepAnimState Pending = PendingRepAnimState.GetValue();
		PendingRepAnimState.Reset();
		ApplyReplicatedAnimState(Pending); // May legitimately re-stash (e.g. generation still ahead).
	}
}

void UPaper2DPlusCharacterProfileComponent::BroadcastReplicatedAnimStateAdvisory(
	FName MoveName, UPaperFlipbook* Flipbook, float PlaybackPosition, FName ConfirmedLabel, bool bAlreadyFinished)
{
	TGuardValue<bool> Guard(bBroadcastingReplicatedAnimState, true);
	OnReplicatedAnimStateChanged.Broadcast(MoveName, Flipbook, PlaybackPosition, ConfirmedLabel, bAlreadyFinished);
}

void UPaper2DPlusCharacterProfileComponent::RebroadcastReplicatedAnimState()
{
	// BIND-THEN-PULL for late binders (TASK-57 U2 review MAJOR fix): initial-bunch RepNotifies land
	// before the actor's BP Event BeginPlay can bind OnReplicatedAnimStateChanged, so a late binder
	// binds first and then calls this to pull the advisory for the last APPLIED state. Re-derives
	// the advisory shape from the committed RepAnimState — NO commits, NO state mutation.
	if (bBroadcastingReplicatedAnimState)
	{
		// Re-entered from inside the advisory broadcast — dropped (the suppress-or-coerce rule;
		// Verbose so automation needs no whitelist).
		UE_LOG(LogPaper2DPlus, Verbose,
			TEXT("RebroadcastReplicatedAnimState re-entered during the advisory broadcast on '%s' — dropped."),
			*GetName());
		return;
	}

	// No-op until a state has APPLIED. The Sequence cross-check covers the stashed-newer-state case:
	// when a newer, not-yet-applied state has overwritten RepAnimState (it sits in
	// PendingRepAnimState awaiting its drain), the last APPLIED snapshot is gone — re-deriving from
	// the unapplied property could advise a move this client never committed; the stash's own drain
	// broadcasts shortly anyway.
	if (LastAppliedSequence == 0 || RepAnimState.Sequence != LastAppliedSequence)
	{
		return;
	}

	const FPaper2DPlusRepAnimState& State = RepAnimState;
	const FFlipbookProfileEntry* Entry =
		State.bIsProfileMove ? FindProfileEntryByMoveName(State.MoveName.ToString()) : nullptr;
	UPaperFlipbook* Flipbook = Entry ? Entry->Identity.Flipbook.Get() : nullptr;
	if (Entry && !Flipbook)
	{
		Flipbook = Entry->Identity.Flipbook.LoadSynchronous();
	}
	if (!Flipbook)
	{
		// The applied state was an authoritative / resolve-failure CLEAR — re-broadcast the same
		// null-move advisory shape.
		BroadcastReplicatedAnimStateAdvisory(NAME_None, nullptr, 0.0f, NAME_None, false);
		return;
	}

	// Anchor-derived position against the CURRENT clock (same math as the apply); anchor -1 or a
	// missing clock degrades to position 0 / not-finished, exactly as documented on the delegate.
	float PlaybackPosition = 0.0f;
	bool bAlreadyFinished = false;
	const TOptional<double> ServerNow = GetServerTimeSecondsForNet();
	if (State.StartServerTime >= 0.0 && ServerNow.IsSet())
	{
		PlaybackPosition = Paper2DPlusNetGating::ComputeReplicatedPlaybackPosition(
			State.StartServerTime, ServerNow.GetValue(), State.PlayRate,
			Flipbook->GetTotalDuration(), State.bLooping, bAlreadyFinished);
	}

	BroadcastReplicatedAnimStateAdvisory(State.MoveName, Flipbook, PlaybackPosition, NAME_None, bAlreadyFinished);
}

#if !UE_BUILD_SHIPPING
void UPaper2DPlusCharacterProfileComponent::SetNetContextOverrideForTests(TOptional<EPaper2DPlusNetContext> InOverride)
{
	NetContextOverrideForTests = InOverride;
}

void UPaper2DPlusCharacterProfileComponent::SetServerTimeOverrideForTests(TOptional<double> InOverride)
{
	ServerTimeOverrideForTests = InOverride;
}
#endif

// ==========================================
// HIT-STOP (TASK-76 PR4)
// ==========================================

// Shared across ALL profile components so overlapping hit-stops (fighting-game trades, gang
// hits) never capture another component's freeze sentinel as an actor's "prior" dilation —
// the cross-component variant of the permanent-freeze bug. First freezer captures the TRUE
// prior; overlapping freezers refcount; the actor unfreezes when the LAST overlapping
// hit-stop ends. Game-thread only (hit-stop freezes/unfreezes are a game-thread mechanism,
// like every CustomTimeDilation write).
struct FPaper2DPlusHitStopRegistry
{
	struct FEntry
	{
		float OriginalDilation = 1.f;
		int32 RefCount = 0;
	};

	static TMap<FObjectKey, FEntry> Entries;

	static void Freeze(AActor* Actor)
	{
		FEntry& Entry = Entries.FindOrAdd(FObjectKey(Actor));
		if (Entry.RefCount == 0)
		{
			// First freezer captures the TRUE prior dilation and applies the sentinel.
			Entry.OriginalDilation = Actor->CustomTimeDilation;
			Actor->CustomTimeDilation = UPaper2DPlusCharacterProfileComponent::HitStopFrozenDilation;
		}
		++Entry.RefCount;
	}

	static void Unfreeze(AActor* Actor) // Actor may be null/invalid (died mid-freeze).
	{
		FEntry* Entry = Entries.Find(FObjectKey(Actor));
		if (!Entry)
		{
			return;
		}

		--Entry->RefCount;
		if (Entry->RefCount <= 0)
		{
			// External CustomTimeDilation writes DURING the freeze are RESPECTED: the captured
			// prior is restored only while the actor still carries the exact freeze sentinel.
			if (IsValid(Actor) && Actor->CustomTimeDilation == UPaper2DPlusCharacterProfileComponent::HitStopFrozenDilation)
			{
				Actor->CustomTimeDilation = Entry->OriginalDilation;
			}
			Entries.Remove(FObjectKey(Actor));
		}
	}
};

TMap<FObjectKey, FPaper2DPlusHitStopRegistry::FEntry> FPaper2DPlusHitStopRegistry::Entries;

void UPaper2DPlusCharacterProfileComponent::FreezeActorOnce(AActor* Actor)
{
	if (!IsValid(Actor))
	{
		return;
	}

	// Per-component dedupe: an actor THIS hit-stop already froze must not be refcounted twice
	// (a multi-hit retrigger folds NEW actors in without inflating the shared registry count).
	for (const TWeakObjectPtr<AActor>& Frozen : FrozenActors)
	{
		if (Frozen.Get() == Actor)
		{
			return;
		}
	}

	FPaper2DPlusHitStopRegistry::Freeze(Actor);
	FrozenActors.Add(Actor);
}

void UPaper2DPlusCharacterProfileComponent::TriggerHitStop(AActor* OtherActor, float DurationSeconds)
{
	if (DurationSeconds <= 0.f)
	{
		return;
	}

	// ─── Authority gate (TASK-57 U5) ────────────────────────────────────
	// On a NETWORKED non-authority context the freeze is authority-published cosmetic state, never
	// client-triggered: warn once + NO-OP (a proxy applies the freeze through OnRep_HitStop instead).
	// Standalone (every worldless/single-player path) and Authority fall through — single-player is
	// byte-identical (the gate passes in Standalone) and the authority runs the existing local freeze.
	const EPaper2DPlusNetContext NetCtx = ResolveNetContext();
	if (NetCtx == EPaper2DPlusNetContext::AutonomousProxy || NetCtx == EPaper2DPlusNetContext::SimulatedProxy)
	{
		if (!bWarnedTriggerHitStopOnNonAuthority)
		{
			bWarnedTriggerHitStopOnNonAuthority = true;
			UE_LOG(LogPaper2DPlus, Warning,
				TEXT("TriggerHitStop called on a non-authority context on '%s' — ignored. Hit-stop is authority-published cosmetic state that self-expires on every machine (TASK-57 U5); clients receive it via replication, never trigger it locally."),
				*GetName());
		}
		return;
	}

	// Re-entrancy guard (Codex F198a): a TriggerHitStop issued from INSIDE an OnHitStopBegin/End handler
	// must reject the WHOLE trigger — local freeze AND publish. BeginHitStopLocal already rejects the
	// re-entrant local freeze (bBroadcastingHitStop), but the publish below would still run, replicating a
	// freeze the authority itself rejected (proxies freeze/extend while the server did not). Reject up front.
	if (bBroadcastingHitStop)
	{
		UE_LOG(LogPaper2DPlus, Warning,
			TEXT("TriggerHitStop re-entered from an OnHitStopBegin/End handler on '%s' — rejected (no local freeze, no publish)."),
			*GetName());
		return;
	}

	// EXTEND vs NEW is decided BEFORE the local freeze mutates bHitStopActive (BeginHitStopLocal flips
	// it) — so the authority publish below stamps a fresh HitStopSeq only on a genuinely new freeze.
	const bool bNewFreeze = !bHitStopActive;

	// The EXISTING local freeze (TASK-76 PR4 body — refcounted registry + real-time ticker + Begin
	// broadcast), now shared with the proxy receive path. Unchanged single-player behavior.
	BeginHitStopLocal(DurationSeconds, OtherActor);

	// ─── Publish (TASK-57 U5/KTD-1) ─────────────────────────────────────
	// Authority + bReplicateHitStop => publish the self-expiring snapshot. KTD-24: when the Victim
	// carries its OWN profile component with bReplicateHitStop, ALSO publish a Victim=self snapshot on
	// the victim's component so the cross-actor pointer relevancy dependency is removed (the refcounted
	// registry already supports overlapping freezes). Standalone publishes nothing (no-op).
	PublishHitStopIfAuthority(DurationSeconds, OtherActor, bNewFreeze);
	if (IsValid(OtherActor))
	{
		if (UPaper2DPlusCharacterProfileComponent* VictimComp = OtherActor->FindComponentByClass<UPaper2DPlusCharacterProfileComponent>())
		{
			if (VictimComp != this && VictimComp->bReplicateHitStop
				&& VictimComp->ResolveNetContext() == EPaper2DPlusNetContext::Authority)
			{
				// KTD-24: publish a Victim=SELF snapshot on the victim's OWN component AND run its own
				// local freeze so that lifecycle (the self-expiring clear + clock re-anchor) is
				// self-managed there — clients who can't see the attacker (relevancy drop) still freeze
				// the victim off the victim's own wire, no cross-actor pointer dependency. The shared
				// refcounted registry tolerates the overlap (the attacker already froze the victim ACTOR
				// once above; this folds the victim component's own refcount on the same actor in — the
				// actor unfreezes only when BOTH ends complete). Victim=self on the wire (the snapshot's
				// frozen victim IS this component's owner).
				const bool bVictimNewFreeze = !VictimComp->bHitStopActive;
				VictimComp->BeginHitStopLocal(DurationSeconds, OtherActor);
				VictimComp->PublishHitStopIfAuthority(DurationSeconds, OtherActor, bVictimNewFreeze);
			}
		}
	}
}

void UPaper2DPlusCharacterProfileComponent::BeginHitStopLocal(float RemainingSeconds, AActor* Victim, bool bReplaceActiveFreeze)
{
	if (RemainingSeconds <= 0.f)
	{
		return; // a stale/expired snapshot (clamped Remaining <= 0) freezes nothing.
	}

	if (bBroadcastingHitStop)
	{
		UE_LOG(LogPaper2DPlus, Warning,
			TEXT("BeginHitStopLocal re-entered from an OnHitStopBegin/End handler on '%s' — rejected."),
			*GetName());
		return;
	}

	// Freeze the owner + the victim (skipping any already frozen — multi-hit retriggers / overlapping
	// replicated snapshots fold NEW actors in without touching captured priors). Null victim tolerated.
	FreezeActorOnce(GetOwner());
	FreezeActorOnce(Victim);

	if (bHitStopActive)
	{
		if (bReplaceActiveFreeze)
		{
			// A genuinely NEW freeze (distinct wire HitStopSeq) landed while a STALE local freeze is still
			// draining — the prior freeze ended on the authority and its clear coalesced away on the wire.
			// REPLACE the remaining (never the stale Max) so this proxy tracks the server's CURRENT freeze,
			// not the elapsed one; the new victim was already folded into the registry above. The original
			// Begin broadcast is kept (no second Begin — the freeze visual continues, only its span corrects).
			//
			// Release STALE victims (Codex F198b): the previous freeze's victim is still in FrozenActors with
			// a live registry ref, so it stays CustomTimeDilation-frozen until THIS freeze ends — even though
			// the authority already cleared it (the new distinct-seq snapshot targets a different victim).
			// Drop every frozen actor that the new snapshot no longer targets (anything that is not the owner
			// or the new Victim), restoring its dilation via the registry, so the frozen set matches the wire.
			AActor* OwnerActor = GetOwner();
			for (int32 i = FrozenActors.Num() - 1; i >= 0; --i)
			{
				AActor* Frozen = FrozenActors[i].GetEvenIfUnreachable();
				if (Frozen == nullptr)
				{
					FrozenActors.RemoveAt(i);
				}
				else if (Frozen != OwnerActor && Frozen != Victim)
				{
					FPaper2DPlusHitStopRegistry::Unfreeze(Frozen);
					FrozenActors.RemoveAt(i);
				}
			}

			HitStopRemainingSeconds = RemainingSeconds;
			ActualFrozenSeconds = 0.f;
			return;
		}
		// Re-trigger / re-snapshot of the SAME freeze (same wire seq, or a local authority re-hit) while
		// active = EXTEND (never shorten) and keep the original Begin broadcast (the extends-never-shortens
		// rule — latest-snapshot-wins on the wire maps to this).
		HitStopRemainingSeconds = FMath::Max(HitStopRemainingSeconds, RemainingSeconds);
		return;
	}

	bHitStopActive = true;          // sets IsHitStopActive() so the U8 drift corrector suppresses (KTD-19).
	HitStopRemainingSeconds = RemainingSeconds;
	ActualFrozenSeconds = 0.f;      // the re-anchor span (KTD-19) starts fresh with each NEW freeze.

	// Un-freeze on REAL time: the core ticker is driven with the UNDILATED app delta, so global/world
	// time-dilation (or the freeze itself) cannot deadlock the unfreeze. LIFETIME CONTRACT: the
	// component is a UObject ticked by a raw lambda — capture only a TWeakObjectPtr so GC/teardown
	// invalidates the capture instead of dangling; EndPlay additionally unregisters by handle.
	// A component GC'd WITHOUT EndPlay strands its registry refcounts until the actors die
	// (in-world components always EndPlay; worldless users must CancelHitStop).
	TWeakObjectPtr<UPaper2DPlusCharacterProfileComponent> WeakThis(this);
	HitStopTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([WeakThis](float DeltaTime)
		{
			UPaper2DPlusCharacterProfileComponent* Comp = WeakThis.Get();
			if (!Comp || !Comp->bHitStopActive)
			{
				return false; // Component gone or already cancelled — unregister.
			}

			// Accumulate the REAL elapsed freeze (the KTD-19 re-anchor amount) from the SAME undilated
			// delta that decrements the remaining time — the move clock (the flipbook) was paused by
			// CustomTimeDilation for exactly this many real seconds.
			Comp->ActualFrozenSeconds += DeltaTime;
			Comp->HitStopRemainingSeconds -= DeltaTime; // DeltaTime accumulates REAL seconds.
			if (Comp->HitStopRemainingSeconds <= 0.f)
			{
				Comp->EndHitStopInternal();
				return false;
			}
			return true;
		}));

	// Broadcast AFTER the dilations are applied so handlers observe the frozen state.
	{
		TGuardValue<bool> Guard(bBroadcastingHitStop, true);
		OnHitStopBegin.Broadcast(Victim, RemainingSeconds);
	}
}

bool UPaper2DPlusCharacterProfileComponent::IsHitStopActive() const
{
	return bHitStopActive;
}

void UPaper2DPlusCharacterProfileComponent::CancelHitStop()
{
	if (!bHitStopActive)
	{
		return; // Safe no-op when inactive (incl. the unconditional EndPlay call).
	}

	// NEVER gate the restore on bBroadcastingHitStop: EndPlay funnels here, and a Begin handler
	// that Destroy()s the attacker reaches this point DURING the Begin broadcast — rejecting would
	// strand the victim frozen forever (the EndPlay-rescue-swallowed bug). EndHitStopInternal
	// always restores; it only suppresses the nested OnHitStopEnd broadcast.
	EndHitStopInternal();
}

void UPaper2DPlusCharacterProfileComponent::EndHitStopInternal()
{
	// THE single restoration path — timer expiry, CancelHitStop, and EndPlay teardown all land here.
	// Restoration is NEVER gated: even a cancel issued from inside an OnHitStopBegin handler (e.g.
	// a handler that Destroy()s the dying attacker -> EndPlay -> CancelHitStop) fully unwinds the
	// freeze — only the nested End BROADCAST is suppressed at the bottom.
	if (HitStopTickerHandle.IsValid())
	{
		// Safe from inside the ticker lambda too — FTSTicker supports removal during a callback.
		FTSTicker::GetCoreTicker().RemoveTicker(HitStopTickerHandle);
		HitStopTickerHandle.Reset();
	}

	for (const TWeakObjectPtr<AActor>& Frozen : FrozenActors)
	{
		// Resolve even unreachable actors so the shared registry entry is still found and its
		// refcount released; the registry IsValid-guards the actual dilation write.
		FPaper2DPlusHitStopRegistry::Unfreeze(Frozen.GetEvenIfUnreachable());
	}
	FrozenActors.Empty();

	// ─── Clock re-anchor (TASK-57 U5/KTD-19) ────────────────────────────
	// THE single unfreeze/restore path: on AUTHORITY, the flipbook (the move clock) was paused by
	// CustomTimeDilation for ActualFrozenSeconds of real time, so advance the published anchor by that
	// span — RepAnimState.StartServerTime += ActualFrozenSeconds. This is a SAME-Sequence byte change
	// (do NOT bump Sequence): the byte delta fires the U8 same-seq OnRep + the drift re-snap so clients
	// re-derive the right position from the re-anchored clock. Gated on a published profile move with a
	// real anchor (a CLEAR / paused / clockless publish has no clock to advance). NEVER touches the
	// LOCAL playback position — the actor's own tick was frozen, so its flipbook resumes correctly on
	// its own; only the WIRE anchor needs catching up. CAVEAT (documented in the contract): any per-actor
	// CustomTimeDilation the game layers as slow-mo breaks this anchor the same way a hit-stop would —
	// whole-WORLD dilation is safe because GetServerWorldTimeSeconds dilates with it.
	const bool bWasAuthority = (ResolveNetContext() == EPaper2DPlusNetContext::Authority);
	if (ActualFrozenSeconds > 0.f
		&& bWasAuthority
		&& RepAnimState.Sequence != 0
		&& RepAnimState.bIsProfileMove
		&& RepAnimState.StartServerTime >= 0.0)
	{
		RepAnimState.StartServerTime += static_cast<double>(ActualFrozenSeconds);
	}

	// Publish a self-expiring CLEAR (Duration=0 on a bumped HitStopSeq) so the "different => apply"
	// receiver detects the end on every machine (TASK-57 U5/KTD-1/KTD-21). THE single end site, so
	// timer expiry, CancelHitStop, AND EndPlay all clear the wire — EndPlay's CancelHitStop guarantees
	// travel/death never carries a live freeze. bReplicateHitStop-gated; no-op on Standalone/proxies.
	if (bWasAuthority)
	{
		PublishHitStopIfAuthority(/*DurationSeconds=*/0.f, /*Victim=*/nullptr, /*bNewFreeze=*/false);
	}

	bHitStopActive = false;
	HitStopRemainingSeconds = 0.f;
	// ActualFrozenSeconds is left as-is here so the test seam / immediate post-unfreeze reads see the
	// span; it is reset on the next NEW freeze (BeginHitStopLocal). The re-anchor above already
	// consumed it (it never double-applies — a second EndHitStopInternal is gated on bHitStopActive).

	if (bBroadcastingHitStop)
	{
		// Ending from inside an OnHitStopBegin/End handler: the state is fully unwound above, but
		// re-broadcasting from inside a broadcast is not safe — suppress the End broadcast only
		// (Verbose so automation needs no expected-message whitelist).
		UE_LOG(LogPaper2DPlus, Verbose,
			TEXT("OnHitStopEnd suppressed (cancelled from inside a hit-stop broadcast) on '%s'"),
			*GetName());
		return;
	}

	{
		TGuardValue<bool> Guard(bBroadcastingHitStop, true);
		OnHitStopEnd.Broadcast();
	}
}

void UPaper2DPlusCharacterProfileComponent::PublishHitStopIfAuthority(float DurationSeconds, AActor* Victim, bool bNewFreeze)
{
	// Authority + opt-in only (KTD-1): Standalone publishes nothing (the local freeze is unchanged),
	// proxies never publish (they receive). bReplicateHitStop is the per-component opt-in (default true).
	if (!bReplicateHitStop || ResolveNetContext() != EPaper2DPlusNetContext::Authority)
	{
		return;
	}

	if (bNewFreeze)
	{
		// A NEW freeze bumps HitStopSeq with the sentinel-skip (the FPaper2DPlusRepHitStop::HitStopSeq
		// contract — 0 is never-published, and a raw wrap onto 0 would be eternally dropped by the
		// "different => apply" receiver). The bumped seq is what makes the receiver treat this as a
		// fresh freeze (not an extend) — though the receiver applies on ANY change via "different".
		++RepHitStop.HitStopSeq;
		if (RepHitStop.HitStopSeq == 0)
		{
			RepHitStop.HitStopSeq = 1;
		}
	}
	// An EXTEND keeps HitStopSeq and only updates Duration (latest-snapshot-wins, the extends-never-
	// shortens rule the local body already enforces). A CLEAR (DurationSeconds<=0) bumps the seq so the
	// receiver's "different => apply" detects the end even if Duration was already 0.
	else if (DurationSeconds <= 0.f)
	{
		++RepHitStop.HitStopSeq;
		if (RepHitStop.HitStopSeq == 0)
		{
			RepHitStop.HitStopSeq = 1;
		}
	}

	RepHitStop.DurationSeconds = FMath::Max(0.f, DurationSeconds);

	if (DurationSeconds > 0.f)
	{
		// Anchor the freeze start at the current server time (the receiver clamps Remaining against it).
		// No clock yet (GameState null) => anchor -1; the receiver then defers/clamps (the clamp goes to
		// Remaining<=0 if Elapsed is garbage — KTD-21). An EXTEND keeps the ORIGINAL StartServerTime so
		// the snapshot's total-duration semantics stay coherent against the anchor (the receiver already
		// holds a freeze; the longer Duration off the same anchor extends it).
		if (bNewFreeze)
		{
			const TOptional<double> ServerNow = GetServerTimeSecondsForNet();
			RepHitStop.StartServerTime = ServerNow.IsSet() ? ServerNow.GetValue() : -1.0;
		}
		RepHitStop.Victim = Victim;
	}
	else
	{
		// CLEAR: Duration 0, no victim. StartServerTime is irrelevant (the receiver ends on Duration 0).
		RepHitStop.Victim = nullptr;
		RepHitStop.StartServerTime = -1.0;
	}
}

void UPaper2DPlusCharacterProfileComponent::RetryPendingRepHitStop()
{
	if (!PendingRepHitStop.IsSet())
	{
		return;
	}
	// Re-stamp RepHitStop with the stashed snapshot and re-drive the receive path. OnRep_HitStop reads
	// RepHitStop (not the stash), so put the stashed value back in place first; it Reset()s the stash on
	// a successful apply, or re-stashes if the clock is STILL missing (latest-wins — bounded retries
	// while the clock is absent are acceptable, they are one-frame deferrals during the join window).
	RepHitStop = PendingRepHitStop.GetValue();
	PendingRepHitStop.Reset();
	OnRep_HitStop();
}


