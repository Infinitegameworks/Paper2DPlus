// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusDebugOverlay.h"

/** FPaper2DPlusDebugOverlay — global console-driven hitbox/frame-data overlay for all Paper2DPlus actors. */

#if !UE_BUILD_SHIPPING

#include "Paper2DPlusBlueprintLibrary.h"
#include "Paper2DPlusAppearanceBudgetSubsystem.h"
#include "Paper2DPlusCharacterProfileComponent.h"
#include "Paper2DPlusTypes.h"
#include "PaperFlipbookComponent.h"
#include "PaperFlipbook.h"
#include "Containers/Ticker.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "UObject/UObjectIterator.h"

namespace
{
	// Dev-only overlays: draw current-frame hitboxes/sockets and per-actor frame
	// data for EVERY actor that carries a UPaper2DPlusCharacterProfileComponent,
	// with no per-actor setup. Both default OFF; cheap-when-off (the ticker
	// early-outs before any iteration when both are 0).
	TAutoConsoleVariable<int32> CVarShowHitboxes(
		TEXT("Paper2DPlus.ShowHitboxes"),
		0,
		TEXT("Dev overlay: draw current-frame attack/hurtboxes + sockets for ALL Paper2DPlus actors (0=off, 1=on)."),
		ECVF_Cheat);

	TAutoConsoleVariable<int32> CVarShowFrameData(
		TEXT("Paper2DPlus.ShowFrameData"),
		0,
		TEXT("Dev overlay: draw '<Flipbook> Frame:<i>/<n>' above ALL Paper2DPlus actors (0=off, 1=on)."),
		ECVF_Cheat);

	TAutoConsoleVariable<int32> CVarShowAppearance(
		TEXT("Paper2DPlus.ShowAppearance"),
		0,
		TEXT("Dev overlay: show aggregate appearance tiers, primitives, queue/cache pressure, build time, load violations, and replicated bytes (0=off, 1=on)."),
		ECVF_Cheat);

	// Handle for the single core ticker registered in Init().
	FTSTicker::FDelegateHandle GTickerHandle;

	/** Pick the world the overlay should draw into: PIE first, then standalone Game. */
	UWorld* ResolveTargetWorld()
	{
		if (!GEngine)
		{
			return nullptr;
		}

		UWorld* GameWorld = nullptr;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (!World)
			{
				continue;
			}

			if (World->WorldType == EWorldType::PIE)
			{
				return World; // PIE wins outright.
			}

			if (World->WorldType == EWorldType::Game && !GameWorld)
			{
				GameWorld = World;
			}
		}

		return GameWorld;
	}

	/** Per-frame driver. Returns true to stay registered. */
	bool TickOverlay(float /*DeltaTime*/)
	{
		const bool bShowHitboxes = CVarShowHitboxes.GetValueOnGameThread() != 0;
		const bool bShowFrameData = CVarShowFrameData.GetValueOnGameThread() != 0;
		const bool bShowAppearance = CVarShowAppearance.GetValueOnGameThread() != 0;

		// Cheap early-out: nothing to do, no iteration, no cost.
		if (!bShowHitboxes && !bShowFrameData && !bShowAppearance)
		{
			return true;
		}

		UWorld* TargetWorld = ResolveTargetWorld();
		if (!TargetWorld)
		{
			return true;
		}

		if (bShowAppearance && GEngine)
		{
			if (const UPaper2DPlusAppearanceBudgetSubsystem* AppearanceBudget =
				TargetWorld->GetSubsystem<UPaper2DPlusAppearanceBudgetSubsystem>())
			{
				GEngine->AddOnScreenDebugMessage(
					0x50324450,
					0.0f,
					FColor::Cyan,
					FPaper2DPlusDebugOverlay::FormatAppearanceStats(AppearanceBudget->GetStatsSnapshot()));
			}
		}

		for (TObjectIterator<UPaper2DPlusCharacterProfileComponent> It; It; ++It)
		{
			UPaper2DPlusCharacterProfileComponent* ProfileComp = *It;
			if (!ProfileComp || ProfileComp->GetWorld() != TargetWorld)
			{
				continue;
			}

			AActor* Owner = ProfileComp->GetOwner();
			if (!Owner)
			{
				continue;
			}

#if ENABLE_DRAW_DEBUG
			if (bShowHitboxes)
			{
				// Reuse the existing pivot-correct draw path (auto-finds the profile
				// component). Duration 0 = single-frame; the ticker repaints next frame.
				UPaper2DPlusBlueprintLibrary::DrawActorDebugHitboxes(
					TargetWorld, Owner, /*Duration*/ 0.f, /*Thickness*/ 2.f, /*bDrawSockets*/ true);
			}

			if (bShowFrameData)
			{
				FString FlipbookName = TEXT("<none>");
				int32 FrameIndex = INDEX_NONE;
				int32 FrameTotal = 0;

				if (UPaperFlipbookComponent* FBComp = ProfileComp->GetResolvedFlipbookComponent())
				{
					if (UPaperFlipbook* Flipbook = FBComp->GetFlipbook())
					{
						FlipbookName = Flipbook->GetName();
						FrameTotal = Flipbook->GetNumKeyFrames();
						// Same resolution the runtime component uses for the current key frame.
						FrameIndex = Flipbook->GetKeyFrameIndexAtTime(FBComp->GetPlaybackPosition());
					}
				}

				const FString Str = FString::Printf(
					TEXT("%s  Frame:%d/%d"),
					*FlipbookName,
					FrameIndex,
					FrameTotal);

				// Draw above the actor. Duration 0 = one frame; ticker repaints.
				DrawDebugString(
					TargetWorld,
					Owner->GetActorLocation() + FVector(0.f, 0.f, 96.f),
					Str,
					/*TestBaseActor*/ nullptr,
					FColor::White,
					/*Duration*/ 0.f,
					/*bDrawShadow*/ true);
			}
#endif // ENABLE_DRAW_DEBUG
		}

		return true;
	}
}

void FPaper2DPlusDebugOverlay::Init()
{
	if (GTickerHandle.IsValid())
	{
		return; // Already initialized.
	}

	// Touch the cvars so they are guaranteed registered before any console use.
	(void)CVarShowHitboxes;
	(void)CVarShowFrameData;
	(void)CVarShowAppearance;

	GTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateStatic(&TickOverlay));
}

void FPaper2DPlusDebugOverlay::Shutdown()
{
	if (GTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(GTickerHandle);
		GTickerHandle.Reset();
	}
}

#else // UE_BUILD_SHIPPING — overlay compiles out entirely; valid empty entry points.

void FPaper2DPlusDebugOverlay::Init()
{
}

void FPaper2DPlusDebugOverlay::Shutdown()
{
}

#endif // !UE_BUILD_SHIPPING

FString FPaper2DPlusDebugOverlay::FormatAppearanceStats(
	const FPaper2DPlusAppearanceStatsSnapshot& Stats)
{
	return FString::Printf(
		TEXT("P2DP Appearance  Registered:%d Visible:%d  Tier D/F/N/L:%d/%d/%d/%d  Prims:%d  Live:%d Pending:%d  Build:%d Queue:%d  Cache:%d/%lldB(reserved) H:%lld M:%lld E:%lld  BuildMs:%.3f WorkMs:%.3f/%.3f U:%d  SyncLoads:%lld  DescSer:%lldB"),
		Stats.RegisteredCount,
		Stats.VisibleCount,
		Stats.DescriptorOnlyCount,
		Stats.FarCompositeCount,
		Stats.NearCompositeCount,
		Stats.ChangingLiveCount,
		Stats.VisiblePrimitiveCount,
		Stats.LiveHandoffsGranted,
		Stats.PendingCount,
		Stats.CompositeBuildUnitsGranted,
		Stats.CompositeQueueDepth,
		Stats.ResidentCacheEntries,
		static_cast<long long>(Stats.CacheReservationBytes),
		static_cast<long long>(Stats.CacheHits),
		static_cast<long long>(Stats.CacheMisses),
		static_cast<long long>(Stats.CacheEvictions),
		Stats.CompositeBuildMilliseconds,
		Stats.CompositeWorkMillisecondsMeasuredThisFrame,
		Stats.CompositeWorkMillisecondsGrantedPerFrame,
		Stats.CompositeBuildUnitsClaimedThisFrame,
		static_cast<long long>(Stats.SynchronousLoadViolations),
		static_cast<long long>(Stats.SerializedAppearanceDescriptorBytes));
}

FString FPaper2DPlusDebugOverlay::FormatAppearanceDecision(
	const FPaper2DPlusAppearanceBudgetDecision& Decision)
{
	return FString::Printf(
		TEXT("Tier:%s Pending:%s Prims:%d Build:%d Cache:%s Key:%s"),
		Paper2DPlusAppearanceBudget::LexToString(Decision.Tier),
		Paper2DPlusAppearanceBudget::LexToString(Decision.PendingReason),
		Decision.VisiblePrimitiveCount,
		Decision.GrantedCompositeBuildUnits,
		Decision.bCacheHit ? TEXT("Hit") : TEXT("Miss"),
		Decision.CacheKeyLabel.IsEmpty() ? TEXT("<none>") : *Decision.CacheKeyLabel);
}
