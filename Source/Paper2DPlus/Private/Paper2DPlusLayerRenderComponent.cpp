// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusLayerRenderComponent.h"
#include "Paper2DPlusModule.h" // LogPaper2DPlus appearance diagnostics
#include "Paper2DPlusAppearanceResolver.h"
#include "Paper2DPlusAppearanceBudgetSubsystem.h"
#include "Paper2DPlusAppearanceCompositeCache.h"
#include "Paper2DPlusAppearanceRenderBackend.h"
#include "Paper2DPlusAppearanceStats.h"
#include "Paper2DPlusNetGating.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "Paper2DPlusCharacterProfileComponent.h"
#include "Paper2DPlusFlipbookComponent.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusLayerDraw.h" // U4: THE shared offset-math home (per-child world offsets)
#include "Paper2DPlusLayerGameplayCompose.h"
#include "Paper2DPlusSettings.h"
#include "PaperSpriteComponent.h"
#include "PaperFlipbook.h"
#include "PaperSprite.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineGlobals.h"
#include "GameFramework/Actor.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/ScopeExit.h"
#include "Serialization/MemoryWriter.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

namespace Paper2DPlusRecolorParamNames
{
	// The material-parameter NAME contract — the Content recolor UMaterial must expose these (a subset is fine; missing
	// names no-op harmlessly). Kept as named constants so the derivation helper and any docs stay in lockstep.
	static const FName PaletteLUT(TEXT("PaletteLUT"));
	static const FName PaletteRow(TEXT("PaletteRow"));
	static const FName RecolorIntensity(TEXT("RecolorIntensity"));
}

namespace
{
	bool Paper2DPlusAppearance_HasSameApplicationWork(
		const FPaper2DPlusAppearanceBudgetDecision& A,
		const FPaper2DPlusAppearanceBudgetDecision& B)
	{
		// Diagnostics-only fields (pending reason, primitive count, cache label/hit) remain fresh through
		// LastAppearanceDecision but do not require rebinding a frame or rewriting component visibility.
		return A.RegistrationId == B.RegistrationId
			&& A.RequestSequence == B.RequestSequence
			&& A.Tier == B.Tier
			&& A.GrantedCompositeBuildUnits == B.GrantedCompositeBuildUnits
			&& A.bGrantedLiveHandoff == B.bGrantedLiveHandoff
			&& A.bRetainPreviousComposite == B.bRetainPreviousComposite
			&& A.bReleaseVisuals == B.bReleaseVisuals;
	}
}

UPaper2DPlusLayerRenderComponent::UPaper2DPlusLayerRenderComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	// Opt-in replication (TASK-57 U6): off by default; BeginPlay calls SetIsReplicated(true) iff bEnableReplication.
	// (Engine already defaults bReplicates false; set it explicitly to mirror the U2 profile-component contract.)
	SetIsReplicatedByDefault(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneFinder(
		TEXT("/Engine/BasicShapes/Plane.Plane"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> DisplayMaterialFinder(
		TEXT("/Paper2D/TranslucentUnlitSpriteMaterial.TranslucentUnlitSpriteMaterial"));
	CompositePlaneMesh = PlaneFinder.Object;
	CompositeDisplayMaterial = DisplayMaterialFinder.Object;
}

void UPaper2DPlusLayerRenderComponent::BeginPlay()
{
	Super::BeginPlay();

	// ─── Networking opt-in (TASK-57 U6, mirrors the U2 profile-component pattern) ──────────────────────
	// bEnableReplication is CONSUMED here: the snapshot is what ResolveNetContext honors for the rest of
	// play (a later flip is inert). There is no client-to-server appearance RPC: the game forwards owner
	// intent over its own channel and calls the setters ON AUTHORITY; the committed result replicates down.
	bReplicationEnabledAtBeginPlay = bEnableReplication;
	if (bEnableReplication)
	{
		SetIsReplicated(true);
	}

	// A dedicated server renders nothing, so the visual layers are pure waste — skip creating them. The
	// authority publication still works, so a dedicated server replicates appearance without visual children.
	const bool bDedicatedServerSkip = bSkipLayerComponentsOnDedicatedServer && IsDedicatedServerContext();

	if (CharacterLayerAsset)
	{
		const ECharacterLayerUsageMode DeliveryMode = CharacterLayerAsset->UsageMode;
		const bool bRuntimeCustomizable = DeliveryMode == ECharacterLayerUsageMode::RuntimeCustomizable;
		const bool bLiveRenderer = bRuntimeCustomizable
			&& !bEnableHybridRuntimeRenderer && !bDedicatedServerSkip && ProfileComponent;

		// Hybrid rendering is explicitly opt-in. Without it, Runtime Customizable remains visibly safe through the
		// exact all-live authored layer renderer. Hybrid actors create live primitives only when the world scheduler
		// grants a bounded handoff; Fixed/Baked draws the registered flipbook output.
		if (bLiveRenderer)
		{
			CreateLayerComponents();
		}
		else
		{
			bLayerComponentsReady = true;
			HideLiveLayerChildren();
		}

		// Runtime Customizable observes both boundaries: all-live compatibility swaps sprites directly, while hybrid
		// uses the same frame event for live fallback and safe prepared-target handoff. No component tick is introduced.
		if (ProfileComponent
			&& DeliveryMode == ECharacterLayerUsageMode::RuntimeCustomizable)
		{
			BindAnimationListeners();
			if (bRuntimeCustomizable)
			{
				if (UPaperFlipbookComponent* FBComp = ProfileComponent->GetResolvedFlipbookComponent())
				{
					CacheRuntimeAnimation(FBComp->GetFlipbook());
				}
			}
		}
		if (IsHybridRuntimeActive())
		{
			RegisterAppearanceScheduler();
		}

		if (bRuntimeCustomizable)
		{
			// Dress-on-spawn seed — proxy-allowed by design: the drained server snapshot below
			// authoritatively replaces the local default on clients.
			TGuardValue<bool> StartupGuard(bApplyingStartupAppearance, true);
			ResetToDefaultAppearance();
		}
	}

	// Apply any replicated snapshot that arrived before the components existed (proxy only — OnRep never runs
	// on the authority, so this is a no-op there). Drained AFTER the default dress-on-spawn so the server's
	// replicated committed appearance authoritatively replaces the local default on clients.
	DrainPendingRepAppearance();
	// Spawn-order hardening: if a committed apply ran while ProfileComponent was unbound (latched in
	// ApplyCommittedAppearanceState), compose the gameplay digest now that both siblings exist.
	RedrivePendingAppearanceCombatPush();
}

void UPaper2DPlusLayerRenderComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnregisterAppearanceScheduler();
	ReleaseHybridVisuals();
	if (ProfileComponent)
	{
		if (UPaper2DPlusFlipbookComponent* FBComp = Cast<UPaper2DPlusFlipbookComponent>(ProfileComponent->FlipbookComponent))
		{
			FBComp->OnFrameChanged.RemoveDynamic(this, &UPaper2DPlusLayerRenderComponent::HandleFrameChanged);
			FBComp->OnFlipbookChanged.RemoveDynamic(this, &UPaper2DPlusLayerRenderComponent::HandleFlipbookChanged);
		}

		// Composed-tier teardown (adversarial finding 1a): push a CLEAR to the profile sibling WHILE the
		// layer asset is still alive (this component's hard CharacterLayerAsset ref roots it until we are
		// GC'd) — the sibling recomposes-to-empty and force-ends any in-flight Layer Cue States immediately.
		// Without this, destroying this component leaves bComposedCombatActive serving stale Layer
		// boxes/events, and the asset's eventual GC turns the sibling's raw event pointers into a
		// use-after-free on the next force-end sweep. No-op if we never pushed.
		if (IsValid(ProfileComponent))
		{
			ProfileComponent->ClearAppearanceCombatDigest();
		}
	}

	DestroyLayerComponents();
	Super::EndPlay(EndPlayReason);
}

void UPaper2DPlusLayerRenderComponent::BindAnimationListeners()
{
	if (!ProfileComponent)
	{
		return;
	}

	UPaper2DPlusFlipbookComponent* FBComp = Cast<UPaper2DPlusFlipbookComponent>(
		ProfileComponent->GetResolvedFlipbookComponent());
	if (!FBComp)
	{
		if (CharacterLayerAsset
			&& CharacterLayerAsset->UsageMode == ECharacterLayerUsageMode::RuntimeCustomizable)
		{
			// U7 (R37): without the enhanced component no delivery path can observe animation boundaries.
			WarnIfLayerSyncBindingUnavailable();
		}
		return;
	}

	// Rebinding is idempotent and also enforces exclusivity if a caller changes UsageMode then refreshes.
	FBComp->OnFrameChanged.RemoveDynamic(this, &UPaper2DPlusLayerRenderComponent::HandleFrameChanged);
	FBComp->OnFlipbookChanged.RemoveDynamic(this, &UPaper2DPlusLayerRenderComponent::HandleFlipbookChanged);
	if (!CharacterLayerAsset
		|| CharacterLayerAsset->UsageMode == ECharacterLayerUsageMode::FixedBaked
		|| (bSkipLayerComponentsOnDedicatedServer && IsDedicatedServerContext()))
	{
		return;
	}

	FBComp->OnFlipbookChanged.AddUniqueDynamic(this, &UPaper2DPlusLayerRenderComponent::HandleFlipbookChanged);
	if (CharacterLayerAsset->UsageMode == ECharacterLayerUsageMode::RuntimeCustomizable)
	{
		FBComp->OnFrameChanged.AddUniqueDynamic(this, &UPaper2DPlusLayerRenderComponent::HandleFrameChanged);
	}
}

FString UPaper2DPlusLayerRenderComponent::ResolveAnimationNameAtBoundary(UPaperFlipbook* NewFlipbook) const
{
	if (!NewFlipbook)
	{
		return FString();
	}

	auto FindName = [NewFlipbook](const UPaper2DPlusCharacterProfileAsset* Profile)
	{
		return Profile ? Profile->GetFlipbookName(NewFlipbook) : FString();
	};

	// Prefer already-resident sources: the assigned ProfileComponent is normally authoritative, and BaseProfile.Get
	// observes an already-loaded soft object without I/O. This keeps the common Runtime Customizable path load-free.
	const UPaper2DPlusCharacterProfileAsset* AssignedProfile = ProfileComponent
		? ProfileComponent->CharacterProfile.Get()
		: nullptr;
	if (FString Name = FindName(AssignedProfile); !Name.IsEmpty())
	{
		return Name;
	}

	UPaper2DPlusCharacterLayerAsset* LayerAsset = CharacterLayerAsset.Get();
	UPaper2DPlusCharacterProfileAsset* LoadedBaseProfile = LayerAsset ? LayerAsset->BaseProfile.Get() : nullptr;
	if (LoadedBaseProfile != AssignedProfile)
	{
		if (FString Name = FindName(LoadedBaseProfile); !Name.IsEmpty())
		{
			return Name;
		}
	}

	// Compatibility fallback only: a Layer Asset can be used without assigning CharacterProfile first. In that
	// malformed/legacy setup the authored animation alias cannot be recovered from the flipbook object alone, so one
	// BaseProfile load is allowed at this flipbook boundary. It is never reached per frame and never warms layer art.
	if (!LoadedBaseProfile && LayerAsset && !LayerAsset->BaseProfile.IsNull())
	{
		if (UPaper2DPlusCharacterProfileAsset* LoadedAtBoundary = LayerAsset->BaseProfile.LoadSynchronous())
		{
			if (FString Name = FindName(LoadedAtBoundary); !Name.IsEmpty())
			{
				return Name;
			}
		}
	}

	// A direct flipbook still supplies a deterministic last-resort identity for conventional profiles.
	return NewFlipbook->GetName();
}

bool UPaper2DPlusLayerRenderComponent::IsHybridRuntimeActive() const
{
	return bEnableHybridRuntimeRenderer
		&& CharacterLayerAsset
		&& CharacterLayerAsset->UsageMode == ECharacterLayerUsageMode::RuntimeCustomizable
		&& !IsDedicatedServerContext();
}

void UPaper2DPlusLayerRenderComponent::SetAppearancePriorityOverride(int32 Priority)
{
	AppearancePriorityOverride = Priority;
	if (UPaper2DPlusAppearanceBudgetSubsystem* Subsystem = AppearanceBudgetSubsystem.Get())
	{
		Subsystem->SetPriorityOverride(this, Priority);
		SubmitAppearanceBudgetRequest();
	}
}

EPaper2DPlusAppearanceTier UPaper2DPlusLayerRenderComponent::GetAppearanceTier() const
{
	if (!IsHybridRuntimeActive() && CharacterLayerAsset
		&& CharacterLayerAsset->UsageMode == ECharacterLayerUsageMode::RuntimeCustomizable
		&& LayerSpriteComponents.Num() > 0)
	{
		return EPaper2DPlusAppearanceTier::ChangingLive;
	}
	return LastAppearanceDecision.Tier;
}

EPaper2DPlusAppearancePendingReason UPaper2DPlusLayerRenderComponent::GetAppearancePendingReason() const
{
	return IsHybridRuntimeActive()
		? LastAppearanceDecision.PendingReason
		: EPaper2DPlusAppearancePendingReason::None;
}

void UPaper2DPlusLayerRenderComponent::RegisterAppearanceScheduler()
{
	// Registration is a new presentation topology generation. Even if request sequence/tier happen to match the
	// prior registration, its first decision must rebuild/reseat visuals rather than hitting steady-state elision.
	bHasAppliedAppearanceDecision = false;
	if (!IsHybridRuntimeActive() || !GetWorld())
	{
		return;
	}
	UPaper2DPlusAppearanceBudgetSubsystem* Subsystem =
		GetWorld()->GetSubsystem<UPaper2DPlusAppearanceBudgetSubsystem>();
	if (!Subsystem)
	{
		return;
	}
	AppearanceBudgetSubsystem = Subsystem;
	AppearanceRegistrationId = Subsystem->RegisterAppearance(
		this,
		FPaper2DPlusAppearanceBudgetDecisionDelegate::CreateUObject(
			this, &ThisClass::ApplyAppearanceBudgetDecision));
	Subsystem->SetPriorityOverride(this, AppearancePriorityOverride);
	SubmitAppearanceBudgetRequest();
}

void UPaper2DPlusLayerRenderComponent::UnregisterAppearanceScheduler()
{
	bHasAppliedAppearanceDecision = false;
	if (UPaper2DPlusAppearanceBudgetSubsystem* Subsystem = AppearanceBudgetSubsystem.Get())
	{
		Subsystem->UnregisterAppearance(this);
	}
	AppearanceBudgetSubsystem.Reset();
	AppearanceRegistrationId = 0;
}

int32 UPaper2DPlusLayerRenderComponent::GetCurrentKeyFrameIndex() const
{
	if (ProfileComponent)
	{
		if (UPaperFlipbookComponent* FBComp = ProfileComponent->GetResolvedFlipbookComponent())
		{
			if (UPaperFlipbook* Flipbook = FBComp->GetFlipbook())
			{
				return FMath::Max(0, Flipbook->GetKeyFrameIndexAtTime(
					FBComp->GetPlaybackPosition(), /*bClampToEnds=*/true));
			}
		}
	}
	return FMath::Max(0, LastObservedFrameIndex);
}

void UPaper2DPlusLayerRenderComponent::CacheRuntimeAnimation(UPaperFlipbook* NewFlipbook)
{
	UndoAppliedChildOffsets();
	CachedEntryProfile.Reset();
	CachedFlipbookEntryIndex = INDEX_NONE;
	CachedAnimationName = ResolveAnimationNameAtBoundary(NewFlipbook);
	UPaper2DPlusCharacterProfileAsset* Profile = ProfileComponent
		? ProfileComponent->CharacterProfile.Get() : nullptr;
	if (!Profile && CharacterLayerAsset)
	{
		Profile = CharacterLayerAsset->BaseProfile.Get();
	}
	if (Profile && NewFlipbook)
	{
		for (int32 Index = 0; Index < Profile->Flipbooks.Num(); ++Index)
		{
			if (Profile->Flipbooks[Index].Identity.Flipbook.Get() == NewFlipbook
				|| Profile->Flipbooks[Index].Identity.FlipbookName.Equals(
					CachedAnimationName, ESearchCase::IgnoreCase))
			{
				CachedEntryProfile = Profile;
				CachedFlipbookEntryIndex = Index;
				break;
			}
		}
	}
	WarmedLayerSprites.Reset();
	if (CharacterLayerAsset && !CachedAnimationName.IsEmpty())
	{
		// The only synchronous art warm is this animation/request boundary. Per-frame reads below always use .Get().
		CharacterLayerAsset->WarmLayerSprites(CachedAnimationName, WarmedLayerSprites);
	}
	LastObservedFrameIndex = 0;
}

bool UPaper2DPlusLayerRenderComponent::BuildCompositeRecipe(
	EPaper2DPlusAppearanceTier Tier,
	FPaper2DPlusAppearanceBuildRecipe& OutRecipe,
	FString& OutError)
{
	OutRecipe = FPaper2DPlusAppearanceBuildRecipe();
	OutError.Reset();
	if (!CharacterLayerAsset || !ProfileComponent || CachedAnimationName.IsEmpty())
	{
		OutError = TEXT("A Layer Asset, Profile Component, and resolved animation are required.");
		return false;
	}
	UPaperFlipbookComponent* FBComp = ProfileComponent->GetResolvedFlipbookComponent();
	UPaperFlipbook* Flipbook = FBComp ? FBComp->GetFlipbook() : nullptr;
	UPaper2DPlusCharacterProfileAsset* Profile = ProfileComponent->CharacterProfile.Get();
	if (!Profile) { Profile = CharacterLayerAsset->BaseProfile.Get(); }
	if (!Flipbook || !Profile)
	{
		OutError = TEXT("The canonical flipbook/profile is not resident at the request boundary.");
		return false;
	}

	OutRecipe.LayerAsset = CharacterLayerAsset;
	OutRecipe.CharacterProfile = Profile;
	OutRecipe.Flipbook = Flipbook;
	OutRecipe.Appearance = VisualAppearance;
	OutRecipe.CanonicalAnimationName = CachedAnimationName;

	TArray<FString> IndependentVisible;
	for (const FString& LayerName : VisualLayerNames)
	{
		const FCharacterLayer* Layer = CharacterLayerAsset->GetLayerByName(LayerName);
		if (Layer && Layer->RuntimeRenderChannel == ECharacterLayerRuntimeRenderChannel::IndependentLive)
		{
			IndependentVisible.Add(LayerName);
		}
	}
	if (Tier == EPaper2DPlusAppearanceTier::NearComposite)
	{
		const UPaper2DPlusSettings* Settings = UPaper2DPlusSettings::Get();
		const int32 KeepLiveCount = FMath::Clamp(
			Settings ? Settings->AppearanceMaxIndependentChannels : 2,
			0,
			FPaper2DPlusAppearanceRenderPolicy::MaxIndependentChannels);
		OutRecipe.IndependentChannelLayerNames = SelectIndependentChannelsForExactPaintOrder(
			VisualLayerNames, IndependentVisible, KeepLiveCount);
	}
	for (const FString& LayerName : VisualLayerNames)
	{
		if (!OutRecipe.IndependentChannelLayerNames.Contains(LayerName))
		{
			OutRecipe.OrderedBaseLayerNames.Add(LayerName);
		}
	}

	const int32 FrameCount = Flipbook->GetNumKeyFrames();
	if (FrameCount <= 0)
	{
		OutError = TEXT("The current flipbook has no key frames.");
		return false;
	}
	OutRecipe.Frames.SetNum(FrameCount);
	float OutputPPU = 1.0f;
	for (int32 FrameIndex = 0; FrameIndex < FrameCount; ++FrameIndex)
	{
		for (const FString& LayerName : OutRecipe.OrderedBaseLayerNames)
		{
			const FCharacterLayer* Layer = CharacterLayerAsset->GetLayerByName(LayerName);
			UPaperSprite* Sprite = CharacterLayerAsset->GetLayerSpriteForFrame(
				LayerName, CachedAnimationName, FrameIndex, /*bAllowSyncLoad=*/false);
			if (!Layer || !Sprite) { continue; } // exact live blank frame => no composite draw
			FPaper2DPlusAppearanceLayerDraw& Draw =
				OutRecipe.Frames[FrameIndex].BaseDraws.AddDefaulted_GetRef();
			Draw.LayerName = LayerName;
			Draw.Sprite = Sprite;
			Draw.TotalOffsetPx = Paper2DPlusLayerDraw::ResolveTotalOffsetPx(
				GetCachedProfileEntry(), FrameIndex, Layer, CachedAnimationName);
			Draw.PaintOrder = OutRecipe.Frames[FrameIndex].BaseDraws.Num() - 1;
			if (const FPaper2DPlusRecolorState* Recolor = RecolorState.Find(LayerName);
				Recolor && RecolorBaseMaterial)
			{
				Draw.bRecolorEnabled = true;
				Draw.bCompositeRecolorContractVerified =
					CharacterLayerAsset->bRuntimeCompositeRecolorContractVerified;
				Draw.RecolorMaterial = RecolorBaseMaterial;
				Draw.PaletteLUT = Recolor->PaletteLUT;
				Draw.PaletteRow = Recolor->PaletteRow;
				Draw.RecolorIntensity = Recolor->Intensity;
			}
			OutputPPU = FMath::Max(OutputPPU, Sprite->GetPixelsPerUnrealUnit());
		}
	}
	OutRecipe.PixelsPerUnrealUnit = FMath::Max(OutputPPU, KINDA_SMALL_NUMBER);

	FVector2D MinPx(TNumericLimits<double>::Max(), TNumericLimits<double>::Max());
	FVector2D MaxPx(TNumericLimits<double>::Lowest(), TNumericLimits<double>::Lowest());
	bool bHasGeometry = false;
	for (const FPaper2DPlusAppearanceFrameRecipe& Frame : OutRecipe.Frames)
	{
		for (const FPaper2DPlusAppearanceLayerDraw& Draw : Frame.BaseDraws)
		{
			const float SpritePPU = FMath::Max(Draw.Sprite->GetPixelsPerUnrealUnit(), KINDA_SMALL_NUMBER);
			for (const FVector4& XYUV : Draw.Sprite->BakedRenderData)
			{
				const FVector2D Point(
					static_cast<double>(XYUV.X) * OutRecipe.PixelsPerUnrealUnit
						+ Draw.TotalOffsetPx.X * OutRecipe.PixelsPerUnrealUnit / SpritePPU,
					-static_cast<double>(XYUV.Y) * OutRecipe.PixelsPerUnrealUnit
						+ Draw.TotalOffsetPx.Y * OutRecipe.PixelsPerUnrealUnit / SpritePPU);
				MinPx.X = FMath::Min(MinPx.X, Point.X);
				MinPx.Y = FMath::Min(MinPx.Y, Point.Y);
				MaxPx.X = FMath::Max(MaxPx.X, Point.X);
				MaxPx.Y = FMath::Max(MaxPx.Y, Point.Y);
				bHasGeometry = true;
			}
		}
	}
	if (bHasGeometry)
	{
		const FVector2D FloorMin(FMath::FloorToDouble(MinPx.X), FMath::FloorToDouble(MinPx.Y));
		const FVector2D CeilMax(FMath::CeilToDouble(MaxPx.X), FMath::CeilToDouble(MaxPx.Y));
		OutRecipe.PixelSize = FIntPoint(
			FMath::Max(1, static_cast<int32>(CeilMax.X - FloorMin.X)),
			FMath::Max(1, static_cast<int32>(CeilMax.Y - FloorMin.Y)));
		OutRecipe.PivotPixels = -FloorMin;
	}
	else
	{
		OutRecipe.PixelSize = FIntPoint(1, 1);
		OutRecipe.PivotPixels = FVector2D::ZeroVector;
	}
	return FPaper2DPlusAppearanceRenderBackend::ValidateRecipe(OutRecipe, OutError);
}

bool UPaper2DPlusLayerRenderComponent::PrepareCompositeForTier(EPaper2DPlusAppearanceTier Tier)
{
	Tier = Tier == EPaper2DPlusAppearanceTier::NearComposite
		? EPaper2DPlusAppearanceTier::NearComposite : EPaper2DPlusAppearanceTier::FarComposite;
	FPaper2DPlusAppearanceBuildRecipe Recipe;
	FString Error;
	if (!BuildCompositeRecipe(Tier, Recipe, Error))
	{
		AppearanceCompositeError = MoveTemp(Error);
		bAppearanceCompositeSupported = false;
		bAppearanceCanAdmitComposite = true;
		PendingCompositeResource = nullptr;
		PendingCompositeKeyLabel.Reset();
		AppearanceRenderPolicy.FailToLiveFallback();
		return false;
	}
	const FPaper2DPlusAppearanceCompositeKey Key =
		FPaper2DPlusAppearanceRenderBackend::MakeCacheKey(Recipe);
	FPaper2DPlusAppearanceCompositeCache& Cache = FPaper2DPlusAppearanceCompositeCache::Get();
	const FPaper2DPlusAppearanceCacheAcquireResult Acquire = Cache.Acquire(Key, Recipe);
	LastObservedCacheMutationSerial = Cache.GetMutationSerial();
	bAppearanceCompositeSupported = true;
	bAppearanceCanAdmitComposite = Acquire.bAdmitted;
	bPendingWasCacheHit = Acquire.bPreparedHit;
	PendingCompositeTier = Tier;
	PendingCompositeResource = Acquire.Resource;
	PendingCompositeKeyLabel = Key.DebugIdentity;
	PendingIndependentChannelNames = Recipe.IndependentChannelLayerNames;
	AppearanceCompositeError.Reset();
	if (!Acquire.bAdmitted || !Acquire.Resource)
	{
		AppearanceRenderPolicy.FailToLiveFallback();
		return false;
	}
	AppearanceRenderPolicy.BeginRequest(
		Acquire.bPreparedHit,
		Acquire.Resource->GetFrameCount());
	return true;
}

void UPaper2DPlusLayerRenderComponent::RequestHybridAppearance(bool bLogicalChange)
{
	if (!IsHybridRuntimeActive()) { return; }
	if (AppearanceRequestSequence < MAX_uint64) { ++AppearanceRequestSequence; }
	bAppearanceChanging |= bLogicalChange;
	const EPaper2DPlusAppearanceTier TargetTier =
		ActiveCompositeTier == EPaper2DPlusAppearanceTier::NearComposite
			? EPaper2DPlusAppearanceTier::NearComposite
			: EPaper2DPlusAppearanceTier::FarComposite;
	PrepareCompositeForTier(TargetTier);
	SubmitAppearanceBudgetRequest();
}

void UPaper2DPlusLayerRenderComponent::SubmitAppearanceBudgetRequest()
{
	UPaper2DPlusAppearanceBudgetSubsystem* Subsystem = AppearanceBudgetSubsystem.Get();
	if (!Subsystem || AppearanceRegistrationId == 0) { return; }
	FPaper2DPlusAppearanceBudgetRequest Request;
	Request.RequestSequence = AppearanceRequestSequence;
	Request.bAppearanceChanging = bAppearanceChanging;
	Request.bNeedsLiveFallback = bAppearanceChanging || !IsValid(ActiveCompositeResource)
		|| ActiveCompositeResource->IsInvalidated();
	Request.bHasPreviousValidComposite = IsValid(ActiveCompositeResource)
		&& !ActiveCompositeResource->IsInvalidated();
	Request.bCompositeReadyForRequestedKey = IsValid(PendingCompositeResource)
		&& PendingCompositeResource->IsPrepared();
	Request.bNeedsCompositeBuild = IsValid(PendingCompositeResource)
		&& !PendingCompositeResource->IsPrepared();
	Request.bCanAdmitComposite = bAppearanceCanAdmitComposite;
	Request.bCompositeSupported = bAppearanceCompositeSupported;
	Request.bCacheHit = bPendingWasCacheHit;
	Request.BlueprintPriority = AppearancePriorityOverride;
	Request.AuthoredLivePrimitiveCount = FMath::Max(1, VisualLayerNames.Num());
	Request.IndependentChannelCount = 0;
	for (const FString& LayerName : VisualLayerNames)
	{
		const FCharacterLayer* Layer = CharacterLayerAsset
			? CharacterLayerAsset->GetLayerByName(LayerName) : nullptr;
		Request.IndependentChannelCount += Layer
			&& Layer->RuntimeRenderChannel == ECharacterLayerRuntimeRenderChannel::IndependentLive ? 1 : 0;
	}
	Request.CacheKeyLabel = PendingCompositeKeyLabel;
	Subsystem->UpdateAppearanceRequest(this, Request);
}

void UPaper2DPlusLayerRenderComponent::BuildGrantedCompositeFrames(int32 GrantedUnits)
{
	if (GrantedUnits <= 0 || !PendingCompositeResource || PendingCompositeResource->IsInvalidated()) { return; }
	FPaper2DPlusAppearanceCompositeCache& Cache = FPaper2DPlusAppearanceCompositeCache::Get();
	const FPaper2DPlusAppearanceBuildRecipe* Recipe = Cache.FindPendingRecipe(PendingCompositeResource);
	if (!Recipe) { return; }
	for (int32 Unit = 0; Unit < GrantedUnits; ++Unit)
	{
		int32 FrameIndex = INDEX_NONE;
		// The cache owns one monotonic cursor per shared pending resource. Multiple components therefore claim distinct
		// frames in O(1) amortized time instead of each granted unit rescanning from key frame zero.
		if (!Cache.TryReserveNextUnbuiltFrame(PendingCompositeResource, GFrameCounter, FrameIndex)) { break; }
		TSharedPtr<FPaper2DPlusAppearanceBuildSubmission, ESPMode::ThreadSafe> Submission;
		FString Error;
		const double StartSeconds = FPlatformTime::Seconds();
		const bool bSubmitted = FPaper2DPlusAppearanceRenderBackend::BuildFrame(
			this, PendingCompositeResource, *Recipe, FrameIndex, Submission, Error);
		Cache.ReportBuildWorkMilliseconds(
			GFrameCounter,
			(FPlatformTime::Seconds() - StartSeconds) * 1000.0);
		if (!bSubmitted || !Cache.QueueSubmittedFrame(PendingCompositeResource, FrameIndex, Submission))
		{
			AppearanceCompositeError = MoveTemp(Error);
			Cache.FailBuild(PendingCompositeResource);
			bAppearanceCompositeSupported = false;
			AppearanceRenderPolicy.FailToLiveFallback();
			break;
		}
	}
	SubmitAppearanceBudgetRequest();
}

void UPaper2DPlusLayerRenderComponent::ApplyAppearanceBudgetDecision(
	const FPaper2DPlusAppearanceBudgetDecision& Decision)
{
	if (Decision.RequestSequence < AppearanceRequestSequence) { return; }
	LastAppearanceDecision = Decision;
	FPaper2DPlusAppearanceCompositeCache& Cache = FPaper2DPlusAppearanceCompositeCache::Get();
	if (!bAppearanceCanAdmitComposite
		&& Cache.GetMutationSerial() != LastObservedCacheMutationSerial)
	{
		if (AppearanceRequestSequence < MAX_uint64) { ++AppearanceRequestSequence; }
		PrepareCompositeForTier(
			ActiveCompositeTier == EPaper2DPlusAppearanceTier::NearComposite
				? EPaper2DPlusAppearanceTier::NearComposite
				: EPaper2DPlusAppearanceTier::FarComposite);
		SubmitAppearanceBudgetRequest();
#if !UE_BUILD_SHIPPING
		++AppearanceDecisionWorkCountForTests;
#endif
		return; // the superseded decision must not apply to the new latest-wins request
	}

	const bool bPendingPrepared = PendingCompositeResource
		&& !PendingCompositeResource->IsInvalidated()
		&& PendingCompositeResource->IsPrepared();
	const bool bResourceInvalidated = (ActiveCompositeResource && ActiveCompositeResource->IsInvalidated())
		|| (PendingCompositeResource && PendingCompositeResource->IsInvalidated());
	if (bHasAppliedAppearanceDecision
		&& Paper2DPlusAppearance_HasSameApplicationWork(Decision, LastAppliedAppearanceDecision)
		&& Decision.GrantedCompositeBuildUnits <= 0
		&& !bPendingPrepared
		&& !bResourceInvalidated)
	{
		// The world subsystem still recomputes view/priority state every frame and the diagnostic decision above is
		// current. Only repeated visual/build projection is skipped. FrameChanged remains the sole steady animation
		// frame bind, so this also avoids rebinding the same active frame twice in one game frame.
#if !UE_BUILD_SHIPPING
		++AppearanceDecisionElisionCountForTests;
#endif
		return;
	}

#if !UE_BUILD_SHIPPING
	++AppearanceDecisionWorkCountForTests;
#endif
	auto MarkDecisionApplied = [this, &Decision]()
	{
		LastAppliedAppearanceDecision = Decision;
		bHasAppliedAppearanceDecision = true;
	};
	bool bNeedsRequestRefresh = bResourceInvalidated;
	if (Decision.bReleaseVisuals)
	{
		ReleaseHybridVisuals();
		SubmitAppearanceBudgetRequest();
		MarkDecisionApplied();
		return;
	}
	if (!PendingCompositeResource && !ActiveCompositeResource)
	{
		if (AppearanceRequestSequence < MAX_uint64) { ++AppearanceRequestSequence; }
		PrepareCompositeForTier(EPaper2DPlusAppearanceTier::FarComposite);
		SubmitAppearanceBudgetRequest();
		if (Decision.Tier != EPaper2DPlusAppearanceTier::ChangingLive)
		{
			MarkDecisionApplied();
			return;
		}
	}
	BuildGrantedCompositeFrames(Decision.GrantedCompositeBuildUnits);
	if (PendingCompositeResource && PendingCompositeResource->IsPrepared())
	{
		AppearanceRenderPolicy.MarkCompositePrepared();
	}
	const int32 FrameIndex = GetCurrentKeyFrameIndex();
	if (PendingCompositeResource && PendingCompositeResource->IsPrepared())
	{
		// A paused or single-key-frame animation may emit no later frame delegate. The scheduler observation of the
		// still-current key frame is a safe matching boundary once every GPU fence has completed.
		TryHybridHandoffAtFrame(FrameIndex);
		// A failed handoff still changed the request's observed resource state from building to ready. A successful
		// handoff already submitted from TryHybridHandoffAtFrame after promoting Pending -> Active.
		bNeedsRequestRefresh |= PendingCompositeResource && PendingCompositeResource->IsPrepared();
	}
	if (Decision.Tier == EPaper2DPlusAppearanceTier::ChangingLive && Decision.bGrantedLiveHandoff)
	{
		ApplyHybridTierVisuals(Decision.Tier, FrameIndex);
	}
	else if (ActiveCompositeResource && !ActiveCompositeResource->IsInvalidated())
	{
		ApplyHybridTierVisuals(ActiveCompositeTier, FrameIndex);
		if ((Decision.Tier == EPaper2DPlusAppearanceTier::NearComposite
				|| Decision.Tier == EPaper2DPlusAppearanceTier::FarComposite)
			&& Decision.Tier != ActiveCompositeTier
			&& (!PendingCompositeResource || PendingCompositeTier != Decision.Tier))
		{
			if (AppearanceRequestSequence < MAX_uint64) { ++AppearanceRequestSequence; }
			PrepareCompositeForTier(Decision.Tier);
			bNeedsRequestRefresh = true;
		}
	}
	if (bNeedsRequestRefresh)
	{
		SubmitAppearanceBudgetRequest();
	}
	MarkDecisionApplied();
}

void UPaper2DPlusLayerRenderComponent::EnsureRuntimeLiveComponents(
	const TArray<FString>& RequiredLayerNames,
	int32 FrameIndex)
{
	if (!GetOwner() || !CharacterLayerAsset) { return; }
	const TSet<FString> Required(RequiredLayerNames);
	TArray<FString> RemoveNames;
	bool bTopologyChanged = false;
	for (const TPair<FString, TObjectPtr<UPaperSpriteComponent>>& Pair : LayerSpriteComponents)
	{
		if (!Required.Contains(Pair.Key)) { RemoveNames.Add(Pair.Key); }
	}
	for (const FString& LayerName : RemoveNames)
	{
		if (TObjectPtr<UPaperSpriteComponent>* Component = LayerSpriteComponents.Find(LayerName))
		{
			if (*Component) { (*Component)->DestroyComponent(); }
		}
		LayerSpriteComponents.Remove(LayerName);
		LayerMIDs.Remove(LayerName);
		LastAppliedChildOffsetPx.Remove(LayerName);
		bTopologyChanged = true;
	}

	AActor* Owner = GetOwner();
	USceneComponent* Root = Owner->GetRootComponent();
	if (!Root)
	{
		if (bTopologyChanged) { UpdateLayerSprites(FrameIndex); }
		return;
	}
	for (const FString& LayerName : RequiredLayerNames)
	{
		if (LayerSpriteComponents.Contains(LayerName)) { continue; }
		const FCharacterLayer* Layer = CharacterLayerAsset->GetLayerByName(LayerName);
		if (!Layer) { continue; }
		const FName ChildName = MakeUniqueObjectName(
			Owner, UPaperSpriteComponent::StaticClass(), FName(*MakeLayerChildBaseName(LayerName)));
		UPaperSpriteComponent* SpriteComp = NewObject<UPaperSpriteComponent>(Owner, ChildName);
		Owner->AddInstanceComponent(SpriteComp);
		SpriteComp->SetupAttachment(Root);
		SpriteComp->SetComponentTickEnabled(false);
		SpriteComp->RegisterComponent();
		LayerSpriteComponents.Add(LayerName, SpriteComp);
		bTopologyChanged = true;
	}
	bLayerComponentsReady = true;
	// FrameChanged already updated every existing child before presentation handoff. Only a structural transition
	// needs initialization here, and it must use the caller's exact event/scheduler frame rather than re-reading a
	// paused or tick-disabled flipbook at frame zero.
	if (bTopologyChanged) { UpdateLayerSprites(FrameIndex); }
}

#if !UE_BUILD_SHIPPING
UPaperSprite* UPaper2DPlusLayerRenderComponent::GetRuntimeLiveSpriteForTests(
	const FString& LayerName) const
{
	const TObjectPtr<UPaperSpriteComponent>* Component = LayerSpriteComponents.Find(LayerName);
	return Component && *Component ? (*Component)->GetSprite() : nullptr;
}
#endif

void UPaper2DPlusLayerRenderComponent::DestroyCompositePrimitive()
{
	if (CompositePrimitive)
	{
		CompositePrimitive->DestroyComponent();
		CompositePrimitive = nullptr;
	}
	CompositeMID = nullptr;
}

void UPaper2DPlusLayerRenderComponent::ReleaseHybridVisuals()
{
	// Direct teardown/RefreshLayers invalidates the visual state represented by LastAppliedAppearanceDecision.
	// A scheduler-driven release marks its own decision applied after this reset.
	bHasAppliedAppearanceDecision = false;
	DestroyCompositePrimitive();
	DestroyLayerComponents();
	PendingCompositeResource = nullptr;
	ActiveCompositeResource = nullptr;
	PendingIndependentChannelNames.Reset();
	ActiveIndependentChannelNames.Reset();
	PendingCompositeKeyLabel.Reset();
	PendingCompositeTier = EPaper2DPlusAppearanceTier::FarComposite;
	ActiveCompositeTier = EPaper2DPlusAppearanceTier::DescriptorOnly;
	bAppearanceChanging = true;
	bAppearanceCanAdmitComposite = true;
	bPendingWasCacheHit = false;
	AppearanceRenderPolicy.Reset();
}

void UPaper2DPlusLayerRenderComponent::ApplyHybridTierVisuals(
	EPaper2DPlusAppearanceTier Tier,
	int32 FrameIndex)
{
	if (Tier == EPaper2DPlusAppearanceTier::ChangingLive)
	{
		if (CompositePrimitive) { CompositePrimitive->SetVisibility(false, true); }
		EnsureRuntimeLiveComponents(VisualLayerNames, FrameIndex);
		const TSet<FString> Visible(VisualLayerNames);
		for (TPair<FString, TObjectPtr<UPaperSpriteComponent>>& Pair : LayerSpriteComponents)
		{
			if (Pair.Value) { Pair.Value->SetVisibility(Visible.Contains(Pair.Key)); }
		}
		return;
	}
	if (!ActiveCompositeResource || ActiveCompositeResource->IsInvalidated()) { return; }
	TArray<FString> LiveChannels;
	if (Tier == EPaper2DPlusAppearanceTier::NearComposite)
	{
		LiveChannels = ActiveIndependentChannelNames;
	}
	EnsureRuntimeLiveComponents(LiveChannels, FrameIndex);
	if (CharacterLayerAsset)
	{
		for (const FString& LayerName : LiveChannels)
		{
			if (TObjectPtr<UPaperSpriteComponent>* Component = LayerSpriteComponents.Find(LayerName);
				Component && *Component)
			{
				const int32 RelativePriority = ResolveIndependentChannelSortPriority(
					VisualLayerNames, LiveChannels, LayerName);
				(*Component)->SetTranslucentSortPriority(RelativePriority);
				const FVector Relative = (*Component)->GetRelativeLocation();
				(*Component)->SetRelativeLocation(FVector(Relative.X, RelativePriority * 0.1f, Relative.Z));
				(*Component)->SetVisibility(true);
			}
		}
	}
	FPaper2DPlusAppearanceRenderBackend::BindPreparedFrame(
		CompositePrimitive, CompositeMID, ActiveCompositeResource, FrameIndex);
}

bool UPaper2DPlusLayerRenderComponent::CanHandoffPendingCompositeAtFrame(int32 FrameIndex) const
{
	return PendingCompositeResource
		&& PendingCompositeResource->IsPrepared()
		&& PendingCompositeResource->IsFrameReady(FrameIndex);
}

void UPaper2DPlusLayerRenderComponent::TryHybridHandoffAtFrame(int32 FrameIndex)
{
	if (!IsHybridRuntimeActive()) { return; }
	if (CanHandoffPendingCompositeAtFrame(FrameIndex))
	{
		if (!CompositePrimitive)
		{
			AActor* Owner = GetOwner();
			USceneComponent* Root = Owner ? Owner->GetRootComponent() : nullptr;
			if (!Owner || !Root) { return; }
			CompositePrimitive = NewObject<UStaticMeshComponent>(Owner, TEXT("Paper2DPlusAppearanceBase"));
			Owner->AddInstanceComponent(CompositePrimitive);
			CompositePrimitive->SetupAttachment(Root);
			FString ConfigureError;
			UMaterialInstanceDynamic* NewMID = nullptr;
			if (!FPaper2DPlusAppearanceRenderBackend::ConfigureCompositePrimitive(
				CompositePrimitive,
				CompositePlaneMesh,
				CompositeDisplayMaterial,
				PendingCompositeResource,
				NewMID,
				ConfigureError))
			{
				AppearanceCompositeError = MoveTemp(ConfigureError);
				DestroyCompositePrimitive();
				return;
			}
			CompositeMID = NewMID;
			CompositePrimitive->RegisterComponent();
		}
		else if (ActiveCompositeResource != PendingCompositeResource)
		{
			FString ConfigureError;
			UMaterialInstanceDynamic* NewMID = nullptr;
			if (!FPaper2DPlusAppearanceRenderBackend::ConfigureCompositePrimitive(
				CompositePrimitive,
				CompositePlaneMesh,
				CompositeDisplayMaterial,
				PendingCompositeResource,
				NewMID,
				ConfigureError))
			{
				AppearanceCompositeError = MoveTemp(ConfigureError);
				return;
			}
			CompositeMID = NewMID;
		}
		if (FPaper2DPlusAppearanceRenderBackend::BindPreparedFrame(
			CompositePrimitive, CompositeMID, PendingCompositeResource, FrameIndex))
		{
			AppearanceRenderPolicy.TryHandoffAtKeyFrame(true);
			ActiveCompositeResource = PendingCompositeResource;
			ActiveCompositeTier = PendingCompositeTier;
			ActiveIndependentChannelNames = PendingIndependentChannelNames;
			PendingCompositeResource = nullptr;
			PendingIndependentChannelNames.Reset();
			bAppearanceChanging = false;
			ApplyHybridTierVisuals(ActiveCompositeTier, FrameIndex);
			SubmitAppearanceBudgetRequest();
		}
	}
	else if (!bAppearanceChanging
		&& ActiveCompositeResource && ActiveCompositeResource->IsFrameReady(FrameIndex))
	{
		// While an exact live handoff is visible, the active resource belongs to the prior descriptor. Reapplying it
		// here would hide the live layers on every frame callback until the pending resource becomes fully prepared.
#if !UE_BUILD_SHIPPING
		++ActiveCompositeFrameRefreshCountForTests;
#endif
		ApplyHybridTierVisuals(ActiveCompositeTier, FrameIndex);
	}
}

void UPaper2DPlusLayerRenderComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	UnregisterAppearanceScheduler();
	DestroyCompositePrimitive();
	// Belt for destructions that never run EndPlay (a never-registered component's DestroyComponent, the
	// worldless rigs): the same teardown CLEAR as EndPlay — idempotent (the profile sibling early-outs
	// once its digest is gone, so the normal EndPlay→destroy path clears exactly once). Guarded IsValid:
	// during whole-actor teardown the sibling may already be dead/garbage.
	if (IsValid(ProfileComponent))
	{
		ProfileComponent->ClearAppearanceCombatDigest();
	}
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

FString UPaper2DPlusLayerRenderComponent::MakeLayerChildBaseName(const FString& LayerName)
{
	// "Layer_" + the authored name with every char that isn't a letter/digit/underscore replaced by '_'
	// (the set illegal in UObject names — spaces, slashes, dots, punctuation from imported art).
	FString Sanitized;
	Sanitized.Reserve(LayerName.Len());
	for (const TCHAR Ch : LayerName)
	{
		Sanitized.AppendChar((FChar::IsAlnum(Ch) || Ch == TEXT('_')) ? Ch : TEXT('_'));
	}
	return FString(TEXT("Layer_")) + Sanitized;
}

int32 UPaper2DPlusLayerRenderComponent::ResolveIndependentChannelSortPriority(
	const TArray<FString>& ResolvedPaintOrder,
	const TArray<FString>& LiveChannelNames,
	const FString& LayerName)
{
	const int32 PaintIndex = ResolvedPaintOrder.IndexOfByKey(LayerName);
	if (PaintIndex == INDEX_NONE) { return 0; }
	int32 LowestBasePaintIndex = MAX_int32;
	int32 HighestBasePaintIndex = INDEX_NONE;
	for (int32 Index = 0; Index < ResolvedPaintOrder.Num(); ++Index)
	{
		if (!LiveChannelNames.Contains(ResolvedPaintOrder[Index]))
		{
			LowestBasePaintIndex = FMath::Min(LowestBasePaintIndex, Index);
			HighestBasePaintIndex = FMath::Max(HighestBasePaintIndex, Index);
		}
	}
	if (HighestBasePaintIndex == INDEX_NONE) { return PaintIndex + 1; }
	return PaintIndex < LowestBasePaintIndex
		? PaintIndex - LowestBasePaintIndex
		: PaintIndex - HighestBasePaintIndex;
}

TArray<FString> UPaper2DPlusLayerRenderComponent::SelectIndependentChannelsForExactPaintOrder(
	const TArray<FString>& ResolvedPaintOrder,
	const TArray<FString>& RequestedIndependentChannels,
	int32 MaxChannels)
{
	TArray<FString> Result;
	MaxChannels = FMath::Clamp(MaxChannels, 0, ResolvedPaintOrder.Num());
	if (MaxChannels == 0 || ResolvedPaintOrder.IsEmpty() || RequestedIndependentChannels.IsEmpty())
	{
		return Result;
	}

	TSet<FString> Requested;
	Requested.Reserve(RequestedIndependentChannels.Num());
	for (const FString& Name : RequestedIndependentChannels)
	{
		Requested.Add(Name);
	}

	int32 LeadingCount = 0;
	while (LeadingCount < ResolvedPaintOrder.Num()
		&& Requested.Contains(ResolvedPaintOrder[LeadingCount]))
	{
		++LeadingCount;
	}
	if (LeadingCount == ResolvedPaintOrder.Num())
	{
		// With no authored base layer, fold a contiguous back prefix and retain the frontmost suffix.
		const int32 FirstLiveIndex = FMath::Max(0, ResolvedPaintOrder.Num() - MaxChannels);
		for (int32 Index = FirstLiveIndex; Index < ResolvedPaintOrder.Num(); ++Index)
		{
			Result.Add(ResolvedPaintOrder[Index]);
		}
		return Result;
	}

	int32 TrailingStart = ResolvedPaintOrder.Num();
	while (TrailingStart > 0
		&& Requested.Contains(ResolvedPaintOrder[TrailingStart - 1]))
	{
		--TrailingStart;
	}

	TBitArray<> Keep(false, ResolvedPaintOrder.Num());
	int32 Remaining = MaxChannels;
	const int32 TrailingToKeep = FMath::Min(Remaining, ResolvedPaintOrder.Num() - TrailingStart);
	for (int32 Index = ResolvedPaintOrder.Num() - TrailingToKeep;
		Index < ResolvedPaintOrder.Num(); ++Index)
	{
		Keep[Index] = true;
	}
	Remaining -= TrailingToKeep;

	// If a leading independent run must be split, only its outermost/backmost prefix is representable:
	// folding a lower channel while retaining a higher one would place that live sprite inside the base interval.
	const int32 LeadingToKeep = FMath::Min(Remaining, LeadingCount);
	for (int32 Index = 0; Index < LeadingToKeep; ++Index)
	{
		Keep[Index] = true;
	}

	for (int32 Index = 0; Index < ResolvedPaintOrder.Num(); ++Index)
	{
		if (Keep[Index])
		{
			Result.Add(ResolvedPaintOrder[Index]);
		}
	}
	return Result;
}

void UPaper2DPlusLayerRenderComponent::CreateLayerComponents()
{
	if (!CharacterLayerAsset || !GetOwner()) return;
	const bool bRuntimeLive = CharacterLayerAsset->UsageMode == ECharacterLayerUsageMode::RuntimeCustomizable;
	if (!bRuntimeLive)
	{
		bLayerComponentsReady = true;
		return;
	}

	AActor* Owner = GetOwner();
	USceneComponent* Root = Owner->GetRootComponent();
	if (!Root) return;

	for (int32 LayerIndex = 0; LayerIndex < CharacterLayerAsset->Layers.Num(); ++LayerIndex)
	{
		const FCharacterLayer& Layer = CharacterLayerAsset->Layers[LayerIndex];
		// Audit F8: sanitize the object NAME (imported layer names can carry spaces/slashes/punctuation,
		// illegal in UObject names) and uniquify so two names sanitizing to the same base stay distinct.
		// The authored LayerName is still the map key below (lookup behavior unchanged).
		const FName ChildName = MakeUniqueObjectName(
			Owner, UPaperSpriteComponent::StaticClass(), FName(*MakeLayerChildBaseName(Layer.LayerName)));
		UPaperSpriteComponent* SpriteComp = NewObject<UPaperSpriteComponent>(Owner, ChildName);
		SpriteComp->SetupAttachment(Root);
		SpriteComp->RegisterComponent();

		const float ZOffset = LayerIndex * 0.1f;
		SpriteComp->SetRelativeLocation(FVector(0.0f, ZOffset, 0.0f));

		bool bVisible = false;
		if (const bool* Override = LayerVisibilityState.Find(Layer.LayerName))
		{
			bVisible = *Override;
		}
		SpriteComp->SetVisibility(bVisible);

		LayerSpriteComponents.Add(Layer.LayerName, SpriteComp);
	}

	bLayerComponentsReady = true; // OnRep can now apply directly instead of stashing (TASK-57 U6).
}

void UPaper2DPlusLayerRenderComponent::DestroyLayerComponents()
{
	for (auto& Pair : LayerSpriteComponents)
	{
		if (Pair.Value)
		{
			Pair.Value->DestroyComponent();
		}
	}
	LayerSpriteComponents.Empty();

	// The per-layer MIDs were owned by (and applied to) the now-destroyed sprite components — drop them so RefreshLayers'
	// rebuilt components re-create fresh MIDs via ApplyRecolor (no stale references, no leak). The transient RecolorState
	// (the user's dye intent) is intentionally left intact so a RefreshLayers re-applies the same dye to the new comps.
	LayerMIDs.Empty();

	// U4: the applied per-child offsets die with the children (a rebuilt child spawns at its base relative
	// location) — clear the tracker so the next apply starts from zero, never subtracting a phantom delta.
	LastAppliedChildOffsetPx.Empty();
}

void UPaper2DPlusLayerRenderComponent::HandleFrameChanged(int32 NewFrameIndex)
{
	LastObservedFrameIndex = FMath::Max(0, NewFrameIndex);
	UpdateLayerSprites(NewFrameIndex);
	if (IsHybridRuntimeActive())
	{
		TryHybridHandoffAtFrame(NewFrameIndex);
	}
}

void UPaper2DPlusLayerRenderComponent::HandleFlipbookChanged(UPaperFlipbook* NewFlipbook)
{
	if (!ProfileComponent || !CharacterLayerAsset)
	{
		return;
	}

	if (CharacterLayerAsset->UsageMode == ECharacterLayerUsageMode::FixedBaked)
	{
		return;
	}
	if (CharacterLayerAsset->UsageMode == ECharacterLayerUsageMode::RuntimeCustomizable)
	{
		CacheRuntimeAnimation(NewFlipbook);
		UpdateLayerSprites(0);
		if (PreviewAppearance.IsSet() || bHasCommittedAppearance)
		{
			RecomputeVisibilityActive();
		}
		else if (IsHybridRuntimeActive())
		{
			RequestHybridAppearance(/*bLogicalChange=*/true);
		}
		return;
	}
}

// ─── One-time layer-sync failure warning (layered-asset redesign U7, R37 drive-by) ─────────────────────

void UPaper2DPlusLayerRenderComponent::WarnLayerSyncFailureOnce(const FString& Cause)
{
	if (bLayerSyncFailureWarned)
	{
		return;
	}
	bLayerSyncFailureWarned = true;
	UE_LOG(LogPaper2DPlus, Warning,
		TEXT("Paper2DPlusLayerRenderComponent on '%s': appearance animation sync is inactive — %s. Legacy sprites and Runtime Customizable animation-scoped visual advisories will not follow the base animation."),
		*GetNameSafe(GetOwner()), *Cause);
}

void UPaper2DPlusLayerRenderComponent::WarnIfLayerSyncBindingUnavailable()
{
	if (!ProfileComponent)
	{
		return; // no profile component is the caller's guard; nothing to diagnose here
	}

	// Diagnose through the LAZY resolver so the cause is named accurately: the raw FlipbookComponent field
	// is null until the profile component's BeginPlay auto-find runs (component order is not guaranteed),
	// and warning "stock component" for a mere ordering race would send the user hunting the wrong bug.
	UPaperFlipbookComponent* Resolved = ProfileComponent->GetResolvedFlipbookComponent();
	if (!Resolved)
	{
		WarnLayerSyncFailureOnce(TEXT("the actor has no flipbook component for the profile component to bind"));
	}
	else if (!Resolved->IsA<UPaper2DPlusFlipbookComponent>())
	{
		WarnLayerSyncFailureOnce(TEXT("the actor's flipbook component is a stock UPaperFlipbookComponent — use UPaper2DPlusFlipbookComponent so frame/flipbook change events reach the layers"));
	}
	else
	{
		WarnLayerSyncFailureOnce(TEXT("the profile component had not resolved its flipbook component when the layer component's BeginPlay ran (component initialization order)"));
	}
}

void UPaper2DPlusLayerRenderComponent::UpdateLayerSprites(int32 FrameIndex)
{
	if (!CharacterLayerAsset || CachedAnimationName.IsEmpty()) return;
	const bool bRuntimeCustomizable =
		CharacterLayerAsset->UsageMode == ECharacterLayerUsageMode::RuntimeCustomizable;
	if (!bRuntimeCustomizable) return;

#if !UE_BUILD_SHIPPING
	++LayerSpriteUpdatePassCountForTests;
#endif

	for (auto& Pair : LayerSpriteComponents)
	{
		if (!Pair.Value) continue;

		// Audit F4/R3: LOAD-FREE per-frame read (bAllowSyncLoad=false) — the sprites were warmed on the
		// flipbook-change path. A not-yet-warmed sprite reads null (fails soft) rather than stalling.
		UPaperSprite* Sprite = CharacterLayerAsset->GetLayerSpriteForFrame(Pair.Key, CachedAnimationName, FrameIndex, /*bAllowSyncLoad=*/false);
		if (!Sprite)
		{
			const FCharacterLayer* Layer = CharacterLayerAsset->GetLayerByName(Pair.Key);
			const FCharacterLayerAnimationMapping* Mapping = Layer
				? Layer->FindAnimationMapping(CachedAnimationName) : nullptr;
			if (Mapping && Mapping->Sprites.IsValidIndex(FrameIndex)
				&& !Mapping->Sprites[FrameIndex].IsNull()
				&& !Mapping->Sprites[FrameIndex].Get())
			{
				// Fail soft without loading; the counter makes a broken request-boundary warm visible in diagnostics.
				Paper2DPlusAppearanceStats::RecordSynchronousLoadViolation();
			}
		}
		Pair.Value->SetSprite(Sprite);
#if !UE_BUILD_SHIPPING
		++LayerSpriteUpdateCountsForTests.FindOrAdd(Pair.Key);
#endif
	}

	// Re-apply recolor after the per-frame SetSprite. On UE 5.7 SetSprite does NOT clear OverrideMaterials (verified:
	// UPaperSpriteComponent::SetSprite only swaps SourceSprite + marks render state dirty), so a MID set on slot 0
	// already survives the swap; this re-apply is a cheap, idempotent belt-and-braces so correctness does not hinge on
	// that holding on every UE 5.0–5.7 version (cross-version BuildPlugin). With no recolor configured it is a no-op.
	ApplyRecolor();

	// U4 (D1): per-child world offsets AFTER the sprite swap (the just-set sprite supplies each child's PPU).
	// Covers both callers — HandleFrameChanged (every key-frame change) and HandleFlipbookChanged (frame 0 of
	// the new animation, applied against the freshly-cached entry after UndoAppliedChildOffsets zeroed out).
	ApplyChildOffsetsForFrame(FrameIndex);
}

const FFlipbookProfileEntry* UPaper2DPlusLayerRenderComponent::GetCachedProfileEntry() const
{
	// Re-resolve per use: the weak profile + IsValidIndex guard make a Flipbooks[] realloc/shrink fail soft
	// (null) instead of dangling — the shared-cache raw-pointer hazard this seam deliberately avoids.
	const UPaper2DPlusCharacterProfileAsset* Profile = CachedEntryProfile.Get();
	if (Profile && Profile->Flipbooks.IsValidIndex(CachedFlipbookEntryIndex))
	{
		return &Profile->Flipbooks[CachedFlipbookEntryIndex];
	}
	return nullptr;
}

void UPaper2DPlusLayerRenderComponent::ApplyChildOffsetsForFrame(int32 FrameIndex)
{
	// Dedicated server (no children created) / worldless rigs without registered children: the loop below is
	// naturally empty — guarded no-op. Never touches sprite pivots; never SetRelativeLocation (offset doc).
	if (!CharacterLayerAsset || LayerSpriteComponents.Num() == 0)
	{
		return;
	}

	const FFlipbookProfileEntry* Entry = GetCachedProfileEntry();
	if (!Entry)
	{
		// CacheRuntimeAnimation already removed the previous animation's applied offsets. Without a registered
		// Profile entry there is no valid animation projection, so do not reapply Layer defaults to sprite-less
		// children under a guessed flipbook name.
		return;
	}

	for (auto& Pair : LayerSpriteComponents)
	{
		UPaperSpriteComponent* Child = Pair.Value;
		if (!Child)
		{
			continue;
		}

		// TOTAL pixel offset through THE shared home: base SpriteOffset+TrimOffset (entry, this frame) + the
		// Layer's authored placement (per-animation override else default). Unknown Layers fail soft.
		const FCharacterLayer* Layer = CharacterLayerAsset->GetLayerByName(Pair.Key);
		const FVector2D NewPx = Paper2DPlusLayerDraw::ResolveTotalOffsetPx(Entry, FrameIndex, Layer, CachedAnimationName);

		// PIXEL-space tracker + BOTH-terms-under-the-CURRENT-basis delta (adversarial finding 2): the
		// already-applied offset lives in the child's RELATIVE transform, so a mirror flip (yaw-180 /
		// negative scale.X) between applications rotates it along — W(LastPx, current basis) IS that
		// rotated applied vector (the conversion's facing sign flip commutes with the parent's world-X
		// mirror), so delta = W(NewPx, cur) − W(LastPx, cur) is exact where the old stored-world compare
		// permanently misapplied by (R−I)*LastWorld. Zero-offset assets never enter the branch below
		// (NewPx == LastPx == zero, tracker stays EMPTY) — no AddWorldOffset call is ever made, pinning
		// the byte-identical no-op the regression test asserts.
		const FVector2D* LastPxPtr = LastAppliedChildOffsetPx.Find(Pair.Key);
		const FVector2D LastPx = LastPxPtr ? *LastPxPtr : FVector2D::ZeroVector;
		if (NewPx.Equals(LastPx))
		{
			// Same pixel offset — under a mirror-type basis change the applied vector already rotated with
			// the child's transform, so there is nothing to correct (the mirror-commutation no-op).
			continue;
		}

		// PPU from THIS CHILD's current sprite (clamped >= 0.001 inside the conversion); no sprite => 1.0,
		// matching the base recipe's default when it can't resolve a sprite.
		float PPU = 1.0f;
		if (UPaperSprite* Sprite = Child->GetSprite())
		{
			PPU = Sprite->GetPixelsPerUnrealUnit();
		}

		// The CHILD's OWN scale and OWN yaw (review contradiction flag: NEVER the parent's or the base
		// flipbook component's) — the facing question is "is THIS rendered sprite mirrored?", and a child
		// inherits the actor flip through its own world transform exactly like the art it renders.
		const FVector ChildScale = Child->GetComponentScale();
		const bool bFacingLeft = UPaper2DPlusCharacterProfileComponent::ResolveFacingLeft(
			ChildScale, Child->GetComponentRotation().Yaw);
		const FVector NewWorld = NewPx.IsNearlyZero()
			? FVector::ZeroVector
			: Paper2DPlusLayerDraw::PixelOffsetToWorld(NewPx, PPU, ChildScale, bFacingLeft);
		const FVector LastWorld = LastPx.IsNearlyZero()
			? FVector::ZeroVector
			: Paper2DPlusLayerDraw::PixelOffsetToWorld(LastPx, PPU, ChildScale, bFacingLeft);

		Child->AddWorldOffset(NewWorld - LastWorld);
		LastAppliedChildOffsetPx.FindOrAdd(Pair.Key) = NewPx;
	}
}

void UPaper2DPlusLayerRenderComponent::UndoAppliedChildOffsets()
{
	// AddWorldOffset(−W(LastPx, CURRENT basis)) per tracked child, then clear — the apply and the record
	// travel together (the F195c rule: never subtract a delta that never moved the component; a child
	// whose component vanished simply drops its record). Converting the stored PIXEL offset under the
	// child's CURRENT basis equals the flip-rotated applied vector when the actor mirror-flipped since
	// the apply (finding 2 — same commutation as ApplyChildOffsetsForFrame). At flipbook-change time the
	// child still holds the OLD animation's sprite, so the PPU matches what the offset was applied under.
	for (const TPair<FString, FVector2D>& Pair : LastAppliedChildOffsetPx)
	{
		if (Pair.Value.IsNearlyZero())
		{
			continue;
		}
		if (const TObjectPtr<UPaperSpriteComponent>* CompPtr = LayerSpriteComponents.Find(Pair.Key))
		{
			if (UPaperSpriteComponent* Child = *CompPtr)
			{
				float PPU = 1.0f;
				if (UPaperSprite* Sprite = Child->GetSprite())
				{
					PPU = Sprite->GetPixelsPerUnrealUnit();
				}
				const FVector ChildScale = Child->GetComponentScale();
				const bool bFacingLeft = UPaper2DPlusCharacterProfileComponent::ResolveFacingLeft(
					ChildScale, Child->GetComponentRotation().Yaw);
				Child->AddWorldOffset(-Paper2DPlusLayerDraw::PixelOffsetToWorld(Pair.Value, PPU, ChildScale, bFacingLeft));
			}
		}
	}
	LastAppliedChildOffsetPx.Empty();
}

void UPaper2DPlusLayerRenderComponent::RecomputeVisibility()
{
	if (bHasCommittedAppearance)
	{
		RecomputeVisibilityUsingDescriptor(CommittedAppearance);
	}
}

bool UPaper2DPlusLayerRenderComponent::CanMutateRuntimeAppearance(bool bEmitDiagnostic)
{
	if (!CharacterLayerAsset)
	{
		return false;
	}
	if (Paper2DPlusAppearanceResolver::AllowsRuntimeMutation(CharacterLayerAsset->UsageMode))
	{
		return true;
	}
	if (bEmitDiagnostic)
	{
		UE_LOG(LogPaper2DPlus, Warning,
			TEXT("Layer appearance on '%s' is Fixed/Baked and cannot be changed at runtime."),
			*GetNameSafe(CharacterLayerAsset));
	}
	for (int32 LayerIndex = 0; LayerIndex < CharacterLayerAsset->Layers.Num(); ++LayerIndex)
	{
		const FCharacterLayer& Layer = CharacterLayerAsset->Layers[LayerIndex];
		LayerVisibilityState.FindOrAdd(Layer.LayerName) = false;
	}
	HideLiveLayerChildren();
	if (ProfileComponent)
	{
		ProfileComponent->ClearAppearanceCombatDigest();
	}
	return false;
}

void UPaper2DPlusLayerRenderComponent::HideLiveLayerChildren()
{
	for (TPair<FString, TObjectPtr<UPaperSpriteComponent>>& Pair : LayerSpriteComponents)
	{
		if (Pair.Value)
		{
			Pair.Value->SetVisibility(false);
		}
	}
}

void UPaper2DPlusLayerRenderComponent::CommitAppearanceState()
{
	if (!CharacterLayerAsset)
	{
		return;
	}
	if (bHasCommittedAppearance
		&& Paper2DPlusAppearanceResolver::IsCompatible(CommittedAppearance, CharacterLayerAsset))
	{
		ApplyCommittedAppearanceState();
		return;
	}
	ResetToDefaultAppearance();
}

bool UPaper2DPlusLayerRenderComponent::CanCommitAppearanceMutation()
{
	// Replicated applies run on proxies by design; the BeginPlay dress-on-spawn seed is proxy-allowed
	// because the drained server snapshot authoritatively replaces it (see the BeginPlay ordering comment).
	if (bApplyingReplicatedAppearance || bApplyingStartupAppearance)
	{
		return true;
	}
	const EPaper2DPlusNetContext NetCtx = ResolveNetContext();
	if (NetCtx == EPaper2DPlusNetContext::AutonomousProxy || NetCtx == EPaper2DPlusNetContext::SimulatedProxy)
	{
		if (!bWarnedAppearanceProxyMutation)
		{
			bWarnedAppearanceProxyMutation = true;
			UE_LOG(LogPaper2DPlus, Warning,
				TEXT("Committed appearance mutation refused on a non-authority context on '%s' — appearance is server-authoritative: call the setters on the server and the committed result replicates down (there is no client->server appearance RPC; see authority-contract.md)."),
				*GetName());
		}
		return false;
	}
	return true;
}

void UPaper2DPlusLayerRenderComponent::CommitRejectedAppearanceSequence(uint16 Sequence)
{
	LastAppliedAppearanceSequence = Sequence;
	if (!bWarnedAppearanceApplySkew)
	{
		bWarnedAppearanceApplySkew = true;
		UE_LOG(LogPaper2DPlus, Warning,
			TEXT("OnRep_AppearanceState: replicated appearance snapshot (seq %u) does not apply on '%s' — server/client Layer-asset skew (incompatible selection or non-RuntimeCustomizable delivery). Sequence committed so the snapshot is not re-admitted; local appearance left unchanged."),
			static_cast<uint32>(Sequence),
			*GetName());
	}
}

void UPaper2DPlusLayerRenderComponent::RedrivePendingAppearanceCombatPush()
{
	if (!bAppearanceCombatPushDeferred || !ProfileComponent || !bHasCommittedAppearance || !CharacterLayerAsset)
	{
		return;
	}
	bAppearanceCombatPushDeferred = false;
	if (Paper2DPlusAppearanceResolver::AllowsGameplayComposition(CommittedAppearance.DeliveryMode))
	{
		ProfileComponent->NotifyAppearanceCombatDirty(CommittedAppearance, CharacterLayerAsset);
	}
	else
	{
		ProfileComponent->ClearAppearanceCombatDigest();
	}
}

bool UPaper2DPlusLayerRenderComponent::CommitGenericAppearanceState(
	const FPaper2DPlusAppearanceDescriptor& Appearance)
{
	// Authority gate at the ONE commit chokepoint (every committed setter funnels here) — a proxy
	// mis-call warns once and no-ops instead of creating client-local gameplay divergence that a
	// same-value server publish would never reconcile.
	if (!CanCommitAppearanceMutation())
	{
		return false;
	}
	if (!CharacterLayerAsset
		|| CharacterLayerAsset->UsageMode != ECharacterLayerUsageMode::RuntimeCustomizable
		|| !Paper2DPlusAppearanceResolver::IsCompatible(Appearance, CharacterLayerAsset))
	{
		return false;
	}

	FPaper2DPlusAppearanceDescriptor Canonical = Appearance;
	Paper2DPlusAppearanceResolver::Normalize(Canonical);
	if (bHasCommittedAppearance && CommittedAppearance == Canonical)
	{
		// A semantic no-op deliberately preserves an active client-local preview and emits no work.
		return true;
	}

	CancelPreviewNoApply();
	CommittedAppearance = MoveTemp(Canonical);
	bHasCommittedAppearance = true;
	ApplyCommittedAppearanceState();
	return true;
}

void UPaper2DPlusLayerRenderComponent::ApplyCommittedAppearanceState()
{
	if (!CharacterLayerAsset || !bHasCommittedAppearance)
	{
		return;
	}

#if !UE_BUILD_SHIPPING
	AppearancePipelineCounterForTests = 0;
	LastAppearanceGameplayOrderForTests = ++AppearancePipelineCounterForTests;
#endif
	if (ProfileComponent)
	{
		bAppearanceCombatPushDeferred = false;
		if (Paper2DPlusAppearanceResolver::AllowsGameplayComposition(CommittedAppearance.DeliveryMode))
		{
			ProfileComponent->NotifyAppearanceCombatDirty(CommittedAppearance, CharacterLayerAsset);
		}
		else
		{
			ProfileComponent->ClearAppearanceCombatDigest();
		}
	}
	else
	{
		// Spawn-order window: the committed descriptor applied before ProfileComponent was bound, so
		// the equipped-layer gameplay digest was not composed. Latch it; the BeginPlay tail re-drives
		// the push once both siblings exist.
		bAppearanceCombatPushDeferred = true;
	}

	if (CommittedAppearance.DeliveryMode == ECharacterLayerUsageMode::FixedBaked)
	{
		for (const FCharacterLayer& Layer : CharacterLayerAsset->Layers)
		{
			LayerVisibilityState.FindOrAdd(Layer.LayerName) = false;
		}
		HideLiveLayerChildren();
		BroadcastLayersChanged();
	}
	else
	{
		RecomputeVisibilityUsingDescriptor(CommittedAppearance);
	}
#if !UE_BUILD_SHIPPING
	LastAppearanceVisualOrderForTests = ++AppearancePipelineCounterForTests;
	LastAppearancePublishOrderForTests = ++AppearancePipelineCounterForTests;
#endif
	PublishAppearanceStateIfAuthority();
}

void UPaper2DPlusLayerRenderComponent::RecomputeVisibilityActive()
{
	if (PreviewAppearance.IsSet())
	{
		RecomputeVisibilityUsingDescriptor(PreviewAppearance.GetValue());
		return;
	}
	if (bHasCommittedAppearance)
	{
		RecomputeVisibilityUsingDescriptor(CommittedAppearance);
		return;
	}
}

void UPaper2DPlusLayerRenderComponent::RecomputeVisibilityUsingDescriptor(
	const FPaper2DPlusAppearanceDescriptor& Appearance)
{
	if (!CharacterLayerAsset
		|| CharacterLayerAsset->UsageMode != ECharacterLayerUsageMode::RuntimeCustomizable)
	{
		return;
	}

	const TArray<FString> Visible = Paper2DPlusAppearanceResolver::ResolveVisibleLayers(
		CharacterLayerAsset, Appearance, CachedAnimationName);
	const TSet<FString> VisibleSet(Visible);
	const bool bDrawLiveChildren = !bEnableHybridRuntimeRenderer;
	VisualAppearance = Appearance;
	VisualLayerNames = Visible; // The asset's Layers array is the sole order authority.

	for (const FCharacterLayer& Layer : CharacterLayerAsset->Layers)
	{
		const bool bVisible = VisibleSet.Contains(Layer.LayerName);
		LayerVisibilityState.FindOrAdd(Layer.LayerName) = bVisible;
		if (TObjectPtr<UPaperSpriteComponent>* CompPtr = LayerSpriteComponents.Find(Layer.LayerName))
		{
			if (*CompPtr)
			{
				(*CompPtr)->SetVisibility(bDrawLiveChildren && bVisible);
			}
		}
	}

	if (bDrawLiveChildren)
	{
		for (int32 VisibleIndex = 0; VisibleIndex < Visible.Num(); ++VisibleIndex)
		{
			if (TObjectPtr<UPaperSpriteComponent>* CompPtr = LayerSpriteComponents.Find(Visible[VisibleIndex]))
			{
				if (*CompPtr)
				{
					const FVector Relative = (*CompPtr)->GetRelativeLocation();
					(*CompPtr)->SetRelativeLocation(FVector(Relative.X, VisibleIndex * 0.1f, Relative.Z));
					(*CompPtr)->SetTranslucentSortPriority(VisibleIndex);
				}
			}
		}
		ApplyRecolor();
	}
	else
	{
		HideLiveLayerChildren();
	}
	if (IsHybridRuntimeActive())
	{
		RequestHybridAppearance(/*bLogicalChange=*/true);
	}
	BroadcastLayersChanged();
}


void UPaper2DPlusLayerRenderComponent::BroadcastLayersChanged()
{
	// Re-entry guard (plugin re-entry-guard-symmetry convention): a handler that calls back into the swap API still
	// updates state via its nested apply, but does not trigger another broadcast, so it cannot recurse forever.
	if (bBroadcastingLayersChanged) return;
	bBroadcastingLayersChanged = true;
#if !UE_BUILD_SHIPPING
	++LayersChangedBroadcastCountForTests;
#endif
	OnLayersChanged.Broadcast();
	bBroadcastingLayersChanged = false;
}


bool UPaper2DPlusLayerRenderComponent::ApplyAppearanceDescriptor(
	const FPaper2DPlusAppearanceDescriptor& Appearance)
{
	if (!CharacterLayerAsset || !Paper2DPlusAppearanceResolver::IsCompatible(Appearance, CharacterLayerAsset))
	{
		return false;
	}
	return CommitGenericAppearanceState(Appearance);
}

bool UPaper2DPlusLayerRenderComponent::ApplyAppearancePreset(FGuid PresetId)
{
	if (!CharacterLayerAsset
		|| CharacterLayerAsset->UsageMode != ECharacterLayerUsageMode::RuntimeCustomizable)
	{
		return false;
	}
	FPaper2DPlusAppearanceDescriptor Candidate;
	return Paper2DPlusAppearanceResolver::BuildPresetDescriptor(
		CharacterLayerAsset, PresetId, Candidate, nullptr)
		&& CommitGenericAppearanceState(Candidate);
}

bool UPaper2DPlusLayerRenderComponent::SetLayerActive(FGuid LayerId, bool bActive)
{
	if (!CharacterLayerAsset
		|| CharacterLayerAsset->UsageMode != ECharacterLayerUsageMode::RuntimeCustomizable)
	{
		return false;
	}

	FPaper2DPlusAppearanceDescriptor Candidate;
	if (bHasCommittedAppearance)
	{
		Candidate = CommittedAppearance;
	}
	else if (!Paper2DPlusAppearanceResolver::BuildDefaultDescriptor(
		CharacterLayerAsset, Candidate, nullptr))
	{
		return false;
	}

	TArray<FGuid> ActiveLayerIds;
	if (!Paper2DPlusAppearanceResolver::ApplyLayerActivation(
		CharacterLayerAsset,
		Candidate.ActiveLayerIds,
		LayerId,
		bActive,
		ActiveLayerIds,
		nullptr))
	{
		return false;
	}
	Candidate.ActiveLayerIds = MoveTemp(ActiveLayerIds);
	return CommitGenericAppearanceState(Candidate);
}

bool UPaper2DPlusLayerRenderComponent::ResetToDefaultAppearance()
{
	if (!CharacterLayerAsset
		|| CharacterLayerAsset->UsageMode != ECharacterLayerUsageMode::RuntimeCustomizable)
	{
		return false;
	}
	FPaper2DPlusAppearanceDescriptor Candidate;
	return Paper2DPlusAppearanceResolver::BuildDefaultDescriptor(
		CharacterLayerAsset, Candidate, nullptr)
		&& CommitGenericAppearanceState(Candidate);
}

bool UPaper2DPlusLayerRenderComponent::IsLayerActive(FGuid LayerId) const
{
	return bHasCommittedAppearance
		&& CommittedAppearance.ActiveLayerIds.Contains(LayerId);
}

TArray<FGuid> UPaper2DPlusLayerRenderComponent::GetActiveLayerIds() const
{
	return bHasCommittedAppearance
		? CommittedAppearance.ActiveLayerIds
		: TArray<FGuid>();
}

// --- Transient try-before-equip preview API (WS4-4C) ---


bool UPaper2DPlusLayerRenderComponent::PreviewLayerActive(FGuid LayerId, bool bActive)
{
	if (!CharacterLayerAsset
		|| CharacterLayerAsset->UsageMode != ECharacterLayerUsageMode::RuntimeCustomizable)
	{
		return false;
	}

	FPaper2DPlusAppearanceDescriptor Candidate;
	if (PreviewAppearance.IsSet())
	{
		Candidate = PreviewAppearance.GetValue();
	}
	else if (bHasCommittedAppearance)
	{
		Candidate = CommittedAppearance;
	}
	else if (!Paper2DPlusAppearanceResolver::BuildDefaultDescriptor(
		CharacterLayerAsset, Candidate, nullptr))
	{
		return false;
	}

	TArray<FGuid> ActiveLayerIds;
	if (!Paper2DPlusAppearanceResolver::ApplyLayerActivation(
		CharacterLayerAsset,
		Candidate.ActiveLayerIds,
		LayerId,
		bActive,
		ActiveLayerIds,
		nullptr))
	{
		return false;
	}
	Candidate.ActiveLayerIds = MoveTemp(ActiveLayerIds);
	PreviewAppearance.Reset();
	PreviewAppearance = MoveTemp(Candidate);
	bPreviewActive = true;
	RecomputeVisibilityUsingDescriptor(PreviewAppearance.GetValue());
	return true;
}

bool UPaper2DPlusLayerRenderComponent::PreviewAppearancePreset(FGuid PresetId)
{
	if (!CharacterLayerAsset
		|| CharacterLayerAsset->UsageMode != ECharacterLayerUsageMode::RuntimeCustomizable)
	{
		return false;
	}
	FPaper2DPlusAppearanceDescriptor Candidate;
	if (!Paper2DPlusAppearanceResolver::BuildPresetDescriptor(
		CharacterLayerAsset, PresetId, Candidate, nullptr))
	{
		return false;
	}
	PreviewAppearance.Reset();
	PreviewAppearance = MoveTemp(Candidate);
	bPreviewActive = true;
	RecomputeVisibilityUsingDescriptor(PreviewAppearance.GetValue());
	return true;
}

void UPaper2DPlusLayerRenderComponent::CommitPreview()
{
	if (!CanMutateRuntimeAppearance()) return;
	if (!bPreviewActive) return;
	if (!PreviewAppearance.IsSet()) return;
	FPaper2DPlusAppearanceDescriptor Candidate = MoveTemp(PreviewAppearance.GetValue());
	PreviewAppearance.Reset();
	bPreviewActive = false;
	CommitGenericAppearanceState(Candidate);
}

void UPaper2DPlusLayerRenderComponent::CancelPreview()
{
	if (!bPreviewActive) return;
	PreviewAppearance.Reset();
	bPreviewActive = false;
	if (bHasCommittedAppearance)
	{
		RecomputeVisibilityUsingDescriptor(CommittedAppearance);
	}
}


bool UPaper2DPlusLayerRenderComponent::IsLayerContributing(FGuid LayerId) const
{
	if (!CharacterLayerAsset || !IsLayerActive(LayerId))
	{
		return false;
	}
	const FCharacterLayer* Layer = CharacterLayerAsset->FindLayerById(LayerId);
	if (!Layer) return false;
	if (const bool* State = LayerVisibilityState.Find(Layer->LayerName))
	{
		return *State;
	}
	return true;
}

void UPaper2DPlusLayerRenderComponent::RefreshLayers()
{
	UnregisterAppearanceScheduler();
	DestroyCompositePrimitive();
	PendingCompositeResource = nullptr;
	ActiveCompositeResource = nullptr;
	AppearanceRenderPolicy.Reset();
	DestroyLayerComponents();
	if (CharacterLayerAsset
		&& CharacterLayerAsset->UsageMode == ECharacterLayerUsageMode::RuntimeCustomizable
		&& !bEnableHybridRuntimeRenderer
		&& !(bEnableReplication && bSkipLayerComponentsOnDedicatedServer && IsDedicatedServerContext()))
	{
		CreateLayerComponents();
	}
	else
	{
		bLayerComponentsReady = true;
		HideLiveLayerChildren();
	}
	// CharacterLayerAsset is Blueprint-assignable and RefreshLayers is the documented re-seat seam. Rebind
	// after every reassignment so Legacy/Runtime/Canonical (and null) cannot retain a prior mode's listeners.
	// BindAnimationListeners removes old delegates before deciding which current-mode delegates to add.
	if (ProfileComponent)
	{
		BindAnimationListeners();
		if (CharacterLayerAsset
			&& CharacterLayerAsset->UsageMode == ECharacterLayerUsageMode::RuntimeCustomizable)
		{
			if (UPaperFlipbookComponent* FBComp = ProfileComponent->GetResolvedFlipbookComponent())
			{
				CacheRuntimeAnimation(FBComp->GetFlipbook());
			}
		}
	}
	if (IsHybridRuntimeActive()) { RegisterAppearanceScheduler(); }

	// Audit F4 (Codex P2): RefreshLayers re-creates the layer components after a layer-asset swap/rebuild WITHOUT
	// a flipbook change, so HandleFlipbookChanged's warm never ran for the (possibly new) mappings. Re-warm the
	// current animation here too, or the next frame change's load-free GetLayerSpriteForFrame(.Get()) would return
	// null for an un-warmed new mapping (cooked/runtime asset-swap) where the pre-F4 sync-load used to recover.
	// No-op when CachedAnimationName is empty (no animation selected yet — UpdateLayerSprites also early-outs).
	WarmedLayerSprites.Reset();
	if (CharacterLayerAsset
		&& CharacterLayerAsset->UsageMode == ECharacterLayerUsageMode::RuntimeCustomizable
		&& !CachedAnimationName.IsEmpty())
	{
		CharacterLayerAsset->WarmLayerSprites(CachedAnimationName, WarmedLayerSprites);
	}

	// Composed-tier re-seat (adversarial finding 1a, reassignment half): RefreshLayers is THE documented
	// entry after reassigning CharacterLayerAsset (there is no setter — the field is BlueprintReadWrite),
	// so re-seat the profile sibling's committed digest against the CURRENT asset here — otherwise the
	// composed gameplay tier keeps serving the previous asset's Layer boxes, and its in-flight ranged
	// events would only ever force-end through a raw pointer after that asset's GC (use-after-free).
	// Mode-appropriate, mirroring the committed chokepoints:
	//   - runtime customization => the committed descriptor + current asset;
	//     the push reads COMMITTED state only, so an active preview cannot leak);
	//   - legacy     => ClearAppearanceCombatDigest — the same CLEAR SetActiveVariant makes (Codex P2:
	//   - fixed/baked delivery => the Profile already contains compiled gameplay, so the Layer tier clears;
	//   - no mode    => ClearAppearanceCombatDigest, which no-ops for actors that never pushed — plain
	//     RefreshLayers callers stay byte-identical.
	// Either path runs the sibling's stale sweep NOW, force-ending the old asset's ranged events while
	// they are still alive (this frame's reassignment hasn't been GC'd yet; the sibling's dead-asset
	// purge is the belt for callers that delayed RefreshLayers across a GC).
	if (ProfileComponent)
	{
		if (bHasCommittedAppearance
			&& Paper2DPlusAppearanceResolver::AllowsGameplayComposition(CommittedAppearance.DeliveryMode))
		{
			ProfileComponent->NotifyAppearanceCombatDirty(CommittedAppearance, CharacterLayerAsset);
		}
		else
		{
			ProfileComponent->ClearAppearanceCombatDigest();
		}
	}

	if (CharacterLayerAsset && CharacterLayerAsset->UsageMode == ECharacterLayerUsageMode::FixedBaked)
	{
		HideLiveLayerChildren();
	}
	else if (PreviewAppearance.IsSet() || bHasCommittedAppearance)
	{
		RecomputeVisibilityActive();
	}
	else
	{
		// Invalid/uncommitted appearances stay hidden; retain recolor intent for the next valid commit.
		ApplyRecolor();
	}

	// Adversarial finding 4: re-apply the CURRENT frame's sprites + per-child offsets NOW instead of
	// waiting for the next frame change — the rebuilt children spawn sprite-less at their base relative
	// locations (the offset tracker died with them in DestroyLayerComponents), which otherwise renders
	// one visibly un-offset frame. UpdateLayerSprites is the existing apply path (sprite swap → recolor
	// → ApplyChildOffsetsForFrame), driven at the flipbook component's live playback position. Zero-offset
	// assets stay byte-identical (the apply branch still never runs); no resolved flipbook / no cached
	// animation = the pre-fix behavior (nothing to re-apply yet).
	if (CharacterLayerAsset
		&& CharacterLayerAsset->UsageMode == ECharacterLayerUsageMode::RuntimeCustomizable
		&& ProfileComponent
		&& !CachedAnimationName.IsEmpty())
	{
		if (UPaperFlipbookComponent* FBComp = ProfileComponent->GetResolvedFlipbookComponent())
		{
			if (UPaperFlipbook* CurrentFlipbook = FBComp->GetFlipbook())
			{
				const int32 CurrentKeyFrame = CurrentFlipbook->GetKeyFrameIndexAtTime(FBComp->GetPlaybackPosition());
				if (CurrentKeyFrame != INDEX_NONE)
				{
					UpdateLayerSprites(CurrentKeyFrame);
				}
			}
		}
	}
}

// --- Per-layer recolor / dye API (WS4-4A) ---

#if WITH_EDITOR
void UPaper2DPlusLayerRenderComponent::Test_RegisterLayerComponent(const FString& LayerName, UPaperSpriteComponent* Comp, bool bVisible)
{
	LayerSpriteComponents.FindOrAdd(LayerName) = Comp;
	LayerVisibilityState.FindOrAdd(LayerName) = bVisible;
	if (Comp) Comp->SetVisibility(bVisible);
	bLayerComponentsReady = true; // worldless rigs register components as if CreateLayerComponents ran (TASK-57 U6 OnRep gate).
}

UMaterialInstanceDynamic* UPaper2DPlusLayerRenderComponent::Test_GetLayerMID(const FString& LayerName) const
{
	if (const TObjectPtr<UMaterialInstanceDynamic>* Found = LayerMIDs.Find(LayerName))
	{
		return *Found;
	}
	return nullptr;
}
#endif

TArray<FPaper2DPlusRecolorParam> UPaper2DPlusLayerRenderComponent::DeriveRecolorParams(const FPaper2DPlusRecolorState& State)
{
	// PURE derivation (the testable seam). The texture param is ALWAYS emitted, even when PaletteLUT is null, so a
	// previously-set LUT is explicitly cleared on the MID rather than left stale; SetTextureParameterValue(null) is a
	// harmless no-op when the material lacks the param. The names here ARE the Content material-parameter contract.
	TArray<FPaper2DPlusRecolorParam> Params;
	Params.Add(FPaper2DPlusRecolorParam::MakeTexture(Paper2DPlusRecolorParamNames::PaletteLUT, State.PaletteLUT));
	Params.Add(FPaper2DPlusRecolorParam::MakeScalar(Paper2DPlusRecolorParamNames::PaletteRow, static_cast<float>(State.PaletteRow)));
	Params.Add(FPaper2DPlusRecolorParam::MakeScalar(Paper2DPlusRecolorParamNames::RecolorIntensity, State.Intensity));
	return Params;
}

void UPaper2DPlusLayerRenderComponent::SetLayerRecolor(const FString& LayerName, const FPaper2DPlusRecolorState& State)
{
	// Transient per-layer dye intent, keyed by the SAME identifier visibility uses (layer name). Re-apply so the change
	// shows immediately; the next recompute/frame also re-applies it (it's resolved from this map, not pinned per-comp).
	RecolorState.FindOrAdd(LayerName) = State;
	ApplyRecolor();
	if (IsHybridRuntimeActive()) { RequestHybridAppearance(/*bLogicalChange=*/true); }
}

bool UPaper2DPlusLayerRenderComponent::SetLayerRecolorById(
	FGuid LayerId,
	const FPaper2DPlusRecolorState& State)
{
	const FCharacterLayer* Layer = CharacterLayerAsset
		? CharacterLayerAsset->FindLayerById(LayerId)
		: nullptr;
	if (!Layer)
	{
		return false;
	}
	SetLayerRecolor(Layer->LayerName, State);
	return true;
}

void UPaper2DPlusLayerRenderComponent::ClearLayerRecolor(const FString& LayerName)
{
	if (RecolorState.Remove(LayerName) > 0)
	{
		// ApplyRecolor sees the now-absent entry and restores that layer's stock material (and drops its MID).
		ApplyRecolor();
		if (IsHybridRuntimeActive()) { RequestHybridAppearance(/*bLogicalChange=*/true); }
	}
}

void UPaper2DPlusLayerRenderComponent::ApplyRecolor()
{
	if (CharacterLayerAsset
		&& CharacterLayerAsset->UsageMode == ECharacterLayerUsageMode::FixedBaked)
	{
		return;
	}
	// POST-resolve pass. NEVER call this from ResolveVisibleLayers — it runs at the END of the visibility/frame sites.
	for (auto& Pair : LayerSpriteComponents)
	{
		UPaperSpriteComponent* Comp = Pair.Value;
		if (!Comp) continue;

		const FString& LayerName = Pair.Key;
		const FPaper2DPlusRecolorState* State = RecolorState.Find(LayerName);

		// A layer is eligible for a dyed MID iff it is VISIBLE, has a recolor entry, and we have a base material to
		// instance. An unknown layer defaults to visible, so a recolor set before the first recompute still applies
		// once the layer shows.
		const bool* Visibility = LayerVisibilityState.Find(LayerName);
		const bool bWantRecolor = (State != nullptr)
			&& (RecolorBaseMaterial != nullptr)
			&& (Visibility == nullptr || *Visibility);

		if (bWantRecolor)
		{
			// Lazily get-or-create the layer's MID (cached, so repeated recomputes reuse it — idempotent, no leak).
			TObjectPtr<UMaterialInstanceDynamic>& MID = LayerMIDs.FindOrAdd(LayerName);
			if (!MID || MID->Parent != RecolorBaseMaterial)
			{
				// (Re)create if missing or if the base material was reassigned out from under a cached MID.
				MID = UMaterialInstanceDynamic::Create(RecolorBaseMaterial, this);
			}

			if (MID)
			{
				for (const FPaper2DPlusRecolorParam& Param : DeriveRecolorParams(*State))
				{
					if (Param.Type == FPaper2DPlusRecolorParam::EType::Texture)
					{
						MID->SetTextureParameterValue(Param.Name, Param.TextureValue);
					}
					else
					{
						MID->SetScalarParameterValue(Param.Name, Param.ScalarValue);
					}
				}
				// Only touch the material slot if it isn't already this MID (SetMaterial already early-outs on equal,
				// but guarding keeps OverrideMaterials growth to recolored layers only).
				if (Comp->GetMaterial(0) != MID)
				{
					Comp->SetMaterial(0, MID);
				}
			}
		}
		else
		{
			// Not recolored (no entry / hidden / null base material). Restore the stock material ONLY if this layer
			// currently has a MID — a layer that never had one is left completely untouched so a clean checkout's
			// OverrideMaterials array stays empty (the byte-identical no-op the regression test pins).
			if (TObjectPtr<UMaterialInstanceDynamic>* ExistingMID = LayerMIDs.Find(LayerName))
			{
				// EmptyOverrideMaterials() (not SetMaterial(0,nullptr)) so the override array SHRINKS back to size 0 —
				// each layer is a dedicated single-material sprite component, so undye is TRUE array-byte-identical to a
				// never-dyed layer, not merely render-identical. GetMaterial(0) then falls through to the stock sprite.
				Comp->EmptyOverrideMaterials();
				LayerMIDs.Remove(LayerName);
			}
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Networking — appearance replication lives on a different component than the
// profile component's U2-U5 work. Committed state replicates through the existing
// chokepoints; previews/manual-visibility/recolor stay local by construction.
// ─────────────────────────────────────────────────────────────────────────────

void UPaper2DPlusLayerRenderComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// UNCONDITIONAL registration (registration is not replication — SetIsReplicated at BeginPlay gates traffic).
	// The committed appearance is server-authoritative state; no COND_ filter (every relevant client
	// needs the dressed appearance, not just the owner).
	DOREPLIFETIME(UPaper2DPlusLayerRenderComponent, RepAppearance);
}

EPaper2DPlusNetContext UPaper2DPlusLayerRenderComponent::GetNetContext() const
{
	return ResolveNetContext();
}

EPaper2DPlusNetContext UPaper2DPlusLayerRenderComponent::ResolveNetContext() const
{
#if !UE_BUILD_SHIPPING
	if (NetContextOverrideForTests.IsSet())
	{
		return NetContextOverrideForTests.GetValue();
	}
#endif

	// The flag is consumed at BeginPlay (SetIsReplicated runs there); the snapshot is what counts afterward.
	const bool bBegun = HasBegunPlay();
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

bool UPaper2DPlusLayerRenderComponent::IsDedicatedServerContext() const
{
#if !UE_BUILD_SHIPPING
	if (DedicatedServerOverrideForTests.IsSet())
	{
		return DedicatedServerOverrideForTests.GetValue();
	}
#endif
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() == NM_DedicatedServer;
}

namespace
{
	/** Payload-v2 equality ignores only the sequence. Format upgrades always publish once. */
	bool AppearanceSnapshotEqualsIgnoringSeq(const FPaper2DPlusRepAppearanceState& A, const FPaper2DPlusRepAppearanceState& B)
	{
		return A.PayloadVersion == 1
			&& B.PayloadVersion == 1
			&& A.Appearance == B.Appearance;
	}
}

bool UPaper2DPlusLayerRenderComponent::ClearLayerRecolorById(FGuid LayerId)
{
	const FCharacterLayer* Layer = CharacterLayerAsset
		? CharacterLayerAsset->FindLayerById(LayerId)
		: nullptr;
	if (!Layer)
	{
		return false;
	}
	ClearLayerRecolor(Layer->LayerName);
	return true;
}

void UPaper2DPlusLayerRenderComponent::PublishAppearanceStateIfAuthority()
{
	// ResolveNetContext is the SINGLE opt-in source of truth: it returns Standalone whenever replication is
	// inactive (the bEnableReplication snapshot consumed at BeginPlay, or no world / NM_Standalone), so a
	// runtime flip stays inert here exactly as the header contract documents. Standalone publishes nothing
	// (single-player is byte-identical); proxies never publish (they receive). Authority only.
	if (ResolveNetContext() != EPaper2DPlusNetContext::Authority)
	{
		return;
	}

	// Payload 2 wraps the ONE descriptor. Migration fields intentionally remain default/empty; a new publisher
	// never mirrors the same selection into the v0/v1 arrays.
	FPaper2DPlusRepAppearanceState Candidate;
	Candidate.PayloadVersion = 1;
	Candidate.Appearance = CommittedAppearance;
	Paper2DPlusAppearanceResolver::Normalize(Candidate.Appearance);

	// No real delta => no bump (a no-op republish must not stomp a client-local preview).
	if (AppearanceSnapshotEqualsIgnoringSeq(RepAppearance, Candidate))
	{
		return;
	}

	// Bump Sequence with the 0=never-published sentinel-skip on wrap (matches the U2/U5 seq discipline).
	uint16 NextSeq = static_cast<uint16>(RepAppearance.Sequence + 1);
	if (NextSeq == 0)
	{
		NextSeq = 1;
	}
	Candidate.Sequence = NextSeq;
	RepAppearance = MoveTemp(Candidate);
	TArray<uint8> SerializedPayload;
	FMemoryWriter PayloadWriter(SerializedPayload, /*bIsPersistent=*/true);
	FPaper2DPlusRepAppearanceState::StaticStruct()->SerializeItem(
		PayloadWriter, &RepAppearance, nullptr);
	Paper2DPlusAppearanceStats::RecordSerializedAppearanceDescriptorBytes(SerializedPayload.Num());
}

void UPaper2DPlusLayerRenderComponent::OnRep_AppearanceState()
{
	// Opt-in gate via the SINGLE net-context source of truth: Standalone = replication inactive (honoring the
	// bEnableReplication snapshot consumed at BeginPlay — a runtime flip stays inert per the header contract).
	// A proxy keeps applying; the authority never receives OnReps so this only ever runs on proxies.
	if (ResolveNetContext() == EPaper2DPlusNetContext::Standalone)
	{
		// Audit F6: a REAL replicated snapshot (Sequence > 0) arriving while replication is inactive locally
		// means the server replicates appearance but this client does not — warn once (mirrors the profile
		// component's archetype-mismatch diagnostic) instead of silently dropping it.
		if (RepAppearance.Sequence != 0 && !bWarnedAppearanceArchetypeMismatch)
		{
			bWarnedAppearanceArchetypeMismatch = true;
			UE_LOG(LogPaper2DPlus, Warning,
				TEXT("OnRep_AppearanceState: replicated appearance data arrived on '%s' while replication is inactive locally — bEnableReplication must match on both sides."),
				*GetName());
		}
		return;
	}

	if (!Paper2DPlusNetGating::ShouldApplyAppearanceSnapshot(
		RepAppearance.Sequence,
		LastAppliedAppearanceSequence,
		RepAppearance.PayloadVersion))
	{
		if (RepAppearance.Sequence != 0
			&& !Paper2DPlusNetGating::IsSupportedAppearancePayload(RepAppearance.PayloadVersion))
		{
			UE_LOG(LogPaper2DPlus, Warning,
				TEXT("OnRep_AppearanceState: unsupported appearance payload version %u on '%s'; state was left unchanged."),
				static_cast<uint32>(RepAppearance.PayloadVersion),
				*GetName());
		}
		return;
	}

	// Components not created yet (pre-BeginPlay) => stash and apply at the BeginPlay tail. Latest-wins (a newer
	// snapshot overwrites an older stash during the join window). NOTE: we deliberately do NOT defer on
	// BaseProfile load state — visible Layers resolve directly from the committed descriptor and Layer asset
	// (it never dereferences BaseProfile; the profile only supplies the animation NAME that anim-scopes CosmeticEffects),
	// and DrainPendingRepAppearance runs only once (the BeginPlay tail), so a profile-pending defer risked
	// permanently stranding a late-join snapshot. Anim-scoped CosmeticEffects self-correct on the first
	// HandleFlipbookChanged (which sets CachedAnimationName and re-resolves via RecomputeVisibilityActive).
	if (!bLayerComponentsReady)
	{
		PendingRepAppearance = RepAppearance;
		return;
	}

	if (ApplyReplicatedAppearance(RepAppearance))
	{
		LastAppliedAppearanceSequence = RepAppearance.Sequence;
	}
	else if (Paper2DPlusNetGating::IsSupportedAppearancePayload(RepAppearance.PayloadVersion))
	{
		// Supported-but-inapplicable (server/client Layer-asset skew): commit the sequence anyway so
		// the same snapshot is not silently re-attempted on every re-received bunch, and warn once —
		// mirrors OnRep_AnimState's skew hardening. An UNSUPPORTED payload deliberately stays
		// uncommitted (unchanged semantics; the version warning above already covers it).
		CommitRejectedAppearanceSequence(RepAppearance.Sequence);
	}
}

bool UPaper2DPlusLayerRenderComponent::ApplyReplicatedAppearance(const FPaper2DPlusRepAppearanceState& Snapshot)
{
	if (!Paper2DPlusNetGating::IsSupportedAppearancePayload(Snapshot.PayloadVersion))
	{
		return false;
	}

	// The replicated apply legitimately runs on proxies — flag it so the commit chokepoint's
	// authority gate passes for exactly this scope.
	TGuardValue<bool> ReplicatedApplyGuard(bApplyingReplicatedAppearance, true);
	return ApplyAppearanceDescriptor(Snapshot.Appearance);
}

void UPaper2DPlusLayerRenderComponent::CancelPreviewNoApply()
{
	if (!bPreviewActive)
	{
		return;
	}
	// The caller immediately applies the replicated committed descriptor.
	PreviewAppearance.Reset();
	bPreviewActive = false;
}

void UPaper2DPlusLayerRenderComponent::DrainPendingRepAppearance()
{
	if (!PendingRepAppearance.IsSet())
	{
		return;
	}
	// Still no components => keep stashed; a later BeginPlay tail / drain retries. (No BaseProfile-pending gate —
	// see OnRep_AppearanceState: the apply doesn't need the profile, and a profile defer here would strand the stash.)
	if (!bLayerComponentsReady)
	{
		return;
	}
	const FPaper2DPlusRepAppearanceState Snapshot = MoveTemp(PendingRepAppearance.GetValue());
	PendingRepAppearance.Reset();
	if (ApplyReplicatedAppearance(Snapshot))
	{
		LastAppliedAppearanceSequence = Snapshot.Sequence;
	}
	else if (Paper2DPlusNetGating::IsSupportedAppearancePayload(Snapshot.PayloadVersion))
	{
		// Same skew hardening as OnRep_AppearanceState: a drained stash that fails a SUPPORTED apply
		// commits its sequence (and warns once) instead of silently wedging. The components-not-ready
		// case above re-stashes and never reaches here.
		CommitRejectedAppearanceSequence(Snapshot.Sequence);
	}
}
