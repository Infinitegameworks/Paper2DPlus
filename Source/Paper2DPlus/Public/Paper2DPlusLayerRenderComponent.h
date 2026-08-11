// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Paper2DPlusAppearanceBudget.h"
#include "Paper2DPlusAppearanceRenderPolicy.h"
#include "Paper2DPlusNetTypes.h"
#include "Paper2DPlusLayerRenderComponent.generated.h"

class UPaper2DPlusCharacterLayerAsset;
class UPaper2DPlusCharacterProfileAsset;
class UPaper2DPlusCharacterProfileComponent;
class UPaperFlipbook;
class UPaperSpriteComponent;
class UPaperSprite;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UTexture2D;
class UStaticMesh;
class UStaticMeshComponent;
class UPaper2DPlusAppearanceBudgetSubsystem;
class UPaper2DPlusAppearanceCompositeResource;
struct FFlipbookProfileEntry;
struct FPaper2DPlusAppearanceBuildRecipe;

/** Fired after the committed or preview Layer selection is projected to visuals. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FPaper2DPlusOnLayersChanged);

/**
 * Tier-1 palette-LUT recolor ("dye") for a single visible layer (WS4-4A). TRANSIENT runtime-only — this struct is
 * carried only in the render component's non-serialized RecolorState map. It is not serialized on the asset,
 * committed into the descriptor, or captured by an Appearance Preset.
 *
 * Tier-1 model: the recolor material samples the sprite's grayscale luminance as the U coordinate into a 1-D
 * gradient-strip texture (PaletteLUT), optionally selecting a row (PaletteRow) in a multi-row LUT atlas. Tier-2
 * channel-mask recolor is OUT of scope — it would be a later add-on on the SAME ApplyRecolor hook.
 */
USTRUCT(BlueprintType)
struct FPaper2DPlusRecolorState
{
	GENERATED_BODY()

	/** The 1-D gradient strip the material indexes by sprite luminance. Null ⇒ the texture param is pushed as null
	 *  (a harmless no-op on a material that has the param; the dye then resolves to whatever the material does with
	 *  a null LUT). The recolor is still "set" — the entry's presence is what drives MID creation, not the LUT. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paper2D+ Recolor")
	TObjectPtr<UTexture2D> PaletteLUT = nullptr;

	/** Selects a row in a multi-row LUT atlas (the material maps this to a V coordinate). 0 for a single-row strip. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paper2D+ Recolor")
	int32 PaletteRow = 0;

	/** Blend strength of the recolor (0 = original art, 1 = full dye). The material lerps by this scalar. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paper2D+ Recolor")
	float Intensity = 1.0f;
};

/**
 * One named (param-name, value) pair derived from an FPaper2DPlusRecolorState — the testable seam between the
 * recolor STATE and the MID. ApplyRecolor pushes each onto the layer's MID via SetScalarParameterValue /
 * SetTextureParameterValue (both no-op harmlessly if the Content material lacks the named param). The set of names
 * here IS the material-parameter contract the Content recolor UMaterial must expose; see DeriveRecolorParams.
 */
struct FPaper2DPlusRecolorParam
{
	enum class EType : uint8 { Scalar, Texture };

	FName Name;
	EType Type = EType::Scalar;
	float ScalarValue = 0.0f;
	TObjectPtr<UTexture2D> TextureValue = nullptr;

	static FPaper2DPlusRecolorParam MakeScalar(FName InName, float InValue)
	{
		FPaper2DPlusRecolorParam P; P.Name = InName; P.Type = EType::Scalar; P.ScalarValue = InValue; return P;
	}
	static FPaper2DPlusRecolorParam MakeTexture(FName InName, UTexture2D* InValue)
	{
		FPaper2DPlusRecolorParam P; P.Name = InName; P.Type = EType::Texture; P.TextureValue = InValue; return P;
	}
};

UCLASS(ClassGroup=(Paper2DPlus), meta=(BlueprintSpawnableComponent, DisplayName="Layer Render"))
class PAPER2DPLUS_API UPaper2DPlusLayerRenderComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPaper2DPlusLayerRenderComponent();

	/** Audit F8: a VALID UObject base name for a layer's child sprite component — "Layer_" + the authored
	 *  layer name with any non-alphanumeric/underscore char replaced by '_' (imported names can carry
	 *  spaces/slashes/punctuation, which are illegal in object names). The authored LayerName remains the
	 *  map key; only the spawned object's name is sanitized. `MakeUniqueObjectName` at the call site then
	 *  keeps two names that sanitize to the same base distinct. Pure/static — worldless-testable. */
	static FString MakeLayerChildBaseName(const FString& LayerName);
	/** Pure near-tier depth projection; independent of legacy modernization gates. Base composite is priority zero. */
	static int32 ResolveIndependentChannelSortPriority(
		const TArray<FString>& ResolvedPaintOrder,
		const TArray<FString>& LiveChannelNames,
		const FString& LayerName);
	/**
	 * Select the bounded live subset that a single base composite can represent without changing paint order.
	 * Live channels may remain only outside the contiguous base interval: a prefix behind it and/or a suffix in
	 * front. When the cap splits an edge run, front channels win first and only the outermost back channels remain.
	 */
	static TArray<FString> SelectIndependentChannelsForExactPaintOrder(
		const TArray<FString>& ResolvedPaintOrder,
		const TArray<FString>& RequestedIndependentChannels,
		int32 MaxChannels);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paper2D+ Layers")
	TObjectPtr<UPaper2DPlusCharacterLayerAsset> CharacterLayerAsset = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paper2D+ Layers")
	TObjectPtr<UPaper2DPlusCharacterProfileComponent> ProfileComponent = nullptr;

	/**
	 * Opt into the bounded client-local base-composite scheduler for Runtime Customizable assets. False is the safe
	 * compatibility mode and renders exact all-live authored layers; gameplay, persistence, and replication are identical.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paper2D+|Appearance")
	bool bEnableHybridRuntimeRenderer = false;

	/** Client-local scheduling priority only. It never changes gameplay, descriptor identity, or replication. */
	UFUNCTION(BlueprintCallable, Category = "Paper2D+|Appearance")
	void SetAppearancePriorityOverride(int32 Priority);

	/** Last scheduler tier. Non-hybrid/legacy components report Changing Live while their exact layers are visible. */
	UFUNCTION(BlueprintPure, Category = "Paper2D+|Appearance")
	EPaper2DPlusAppearanceTier GetAppearanceTier() const;

	/** Why the requested hybrid representation is pending. */
	UFUNCTION(BlueprintPure, Category = "Paper2D+|Appearance")
	EPaper2DPlusAppearancePendingReason GetAppearancePendingReason() const;

	/** True when an active Layer contributes authored art to the current animation. */
	UFUNCTION(BlueprintPure, Category = "Paper2D+|Appearance")
	bool IsLayerContributing(FGuid LayerId) const;

	UFUNCTION(BlueprintCallable, Category = "Paper2D+|Layers")
	void RefreshLayers();

	/** The one committed descriptor consumed by gameplay, persistence, replication, and visual backends. */
	UFUNCTION(BlueprintPure, Category = "Paper2D+|Appearance")
	FPaper2DPlusAppearanceDescriptor GetAppearanceDescriptor() const { return CommittedAppearance; }

	/** Replace the committed selection with one complete authored Appearance Preset. Runtime Customizable only. */
	UFUNCTION(BlueprintCallable, Category = "Paper2D+|Appearance")
	bool ApplyAppearancePreset(FGuid PresetId);

	/** Activate/deactivate one stable Layer identity. Activating an Exclusive Group peer removes the former peer. */
	UFUNCTION(BlueprintCallable, Category = "Paper2D+|Appearance")
	bool SetLayerActive(FGuid LayerId, bool bActive);

	/** Restore the asset's one required Default Appearance. Runtime Customizable only. */
	UFUNCTION(BlueprintCallable, Category = "Paper2D+|Appearance")
	bool ResetToDefaultAppearance();

	/** True when LayerId belongs to the current committed selection, independent of animation contribution. */
	UFUNCTION(BlueprintPure, Category = "Paper2D+|Appearance")
	bool IsLayerActive(FGuid LayerId) const;

	/** Stable selected Layer identities in the asset's global order. */
	UFUNCTION(BlueprintPure, Category = "Paper2D+|Appearance")
	TArray<FGuid> GetActiveLayerIds() const;

	/** Restore a stored descriptor whose delivery mode and Layer selection match the assigned Layer Asset. */
	UFUNCTION(BlueprintCallable, Category = "Paper2D+|Appearance")
	bool ApplyAppearanceDescriptor(const FPaper2DPlusAppearanceDescriptor& Appearance);

	// --- Per-layer recolor / dye API (WS4-4A) ---
	//
	// Transient runtime-only, keyed by Layer name. This state is never serialized and an Appearance Preset
	// does not capture it. Recolor is applied as a post-resolve pass on every visibility/frame
	// mutation: a VISIBLE layer with a recolor entry gets a shared MID of RecolorBaseMaterial with the Tier-1 params;
	// a layer with no entry keeps the stock sprite material. When RecolorBaseMaterial is null the whole system is a
	// provably byte-identical no-op (no MID is ever created, no material slot is ever touched).

	/** Set (or overwrite) the transient recolor for a layer, keyed by LAYER NAME. Re-applies the recolor pass. */
	UFUNCTION(BlueprintCallable, Category = "Paper2D+|Layers")
	void SetLayerRecolor(const FString& LayerName, const FPaper2DPlusRecolorState& State);

	/** Stable-ID recolor entry point for the generic appearance model. Presentation-only and never published. */
	UFUNCTION(BlueprintCallable, Category = "Paper2D+|Appearance")
	bool SetLayerRecolorById(FGuid LayerId, const FPaper2DPlusRecolorState& State);

	/** Clear a layer's transient recolor, restoring its stock sprite material. Re-applies the recolor pass. */
	UFUNCTION(BlueprintCallable, Category = "Paper2D+|Layers")
	void ClearLayerRecolor(const FString& LayerName);

	/** Clear presentation-only recolor by stable Layer identity. */
	UFUNCTION(BlueprintCallable, Category = "Paper2D+|Appearance")
	bool ClearLayerRecolorById(FGuid LayerId);

	/**
	 * Base recolor material the per-layer MIDs are instanced from (WS4-4A). NULL-SAFE and user-assignable: the actual
	 * Tier-1 palette-LUT recolor UMaterial lives in Content and is NEVER staged by the plugin — assign it here per
	 * project. While this is null the recolor system is a byte-identical no-op (no MID, stock material kept). Deliberately
	 * NOT a ConstructorHelpers::FObjectFinder to a Content path — that would hard-depend on an unstaged asset and assert
	 * on a clean checkout.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Paper2D+ Recolor")
	TObjectPtr<UMaterialInterface> RecolorBaseMaterial = nullptr;

	/**
	 * The testable seam: derive the named (param, value) set a recolor MID needs from a recolor state. PURE/static —
	 * no component/world state — so it is asserted worldlessly. The names returned here are the material-parameter
	 * CONTRACT the Content recolor UMaterial must expose:
	 *   - Texture "PaletteLUT"      = State.PaletteLUT (always emitted, even when null)
	 *   - Scalar  "PaletteRow"      = State.PaletteRow
	 *   - Scalar  "RecolorIntensity"= State.Intensity
	 * SetTextureParameterValue/SetScalarParameterValue no-op harmlessly if the material lacks a name, so a material
	 * that exposes only a subset still works.
	 */
	static TArray<FPaper2DPlusRecolorParam> DeriveRecolorParams(const FPaper2DPlusRecolorState& State);

#if WITH_EDITOR
	// --- Test-only seams (WS4-4A worldless tests). Editor-only so the shipping runtime surface stays clean. They let a
	// worldless test register a UPaperSpriteComponent (no UWorld) and observe the recolor pass's effect on it.
	/** Register a Layer's sprite component as if CreateLayerComponents had made it (worldless tests don't run BeginPlay).
	 *  Also seeds the transient presentation state used by recolor. */
	void Test_RegisterLayerComponent(const FString& LayerName, UPaperSpriteComponent* Comp, bool bVisible);
	/** True if the named layer currently has a cached recolor MID. */
	bool Test_HasLayerMID(const FString& LayerName) const { return LayerMIDs.Contains(LayerName); }
	/** The cached recolor MID for the named layer (or null). */
	UMaterialInstanceDynamic* Test_GetLayerMID(const FString& LayerName) const;
	/** Number of cached recolor MIDs (for leak/idempotency assertions). */
	int32 Test_NumLayerMIDs() const { return LayerMIDs.Num(); }
	/** Run the POST-resolve recolor pass directly (worldless tests drive it without a full recompute). */
	void Test_ApplyRecolor() { ApplyRecolor(); }
	/** Drive the per-frame SetSprite path (UpdateLayerSprites) for the MID-survival test. UpdateLayerSprites early-outs
	 *  unless an animation is cached, so seed one first via Test_SetCachedAnimationName. */
	void Test_UpdateLayerSprites(int32 FrameIndex) { UpdateLayerSprites(FrameIndex); }
	/** Seed CachedAnimationName so Test_UpdateLayerSprites runs its body (the MID-survival re-apply). */
	void Test_SetCachedAnimationName(const FString& Name) { CachedAnimationName = Name; }
	/** Drive the preview-aware appearance recompute directly (the path HandleFlipbookChanged/RefreshLayers use). Lets a
	 *  worldless test prove an active preview survives a visibility refresh without needing a ProfileComponent/flipbook
	 *  to fire the real HandleFlipbookChanged (PR#155). */
	void Test_RecomputeVisibilityActive() { RecomputeVisibilityActive(); }
	/** Bind the same delivery-mode-aware animation listeners as BeginPlay, without registering a worldless component. */
	void Test_BindAnimationListeners() { BindAnimationListeners(); }
	/** Current logical animation identity observed by the appearance path. */
	const FString& Test_GetCachedAnimationName() const { return CachedAnimationName; }
	/** Number of sprite references warmed at the current animation boundary. */
	int32 Test_NumWarmedLayerSprites() const { return WarmedLayerSprites.Num(); }
	/** Number of logical visual advisories emitted by this component. */
	uint32 Test_GetLayersChangedBroadcastCount() const { return LayersChangedBroadcastCountForTests; }
	// --- U4 per-child offset test seams (the delegate-bound handlers are private UFUNCTIONs; worldless
	// rigs can't fire the real flipbook-component delegates without a world). ---
	/** Drive the real delivery-mode-aware flipbook-change path. Runtime Customizable updates animation scope and
	 *  live children; Fixed/Baked is inert. */
	void Test_HandleFlipbookChanged(UPaperFlipbook* NewFlipbook) { HandleFlipbookChanged(NewFlipbook); }
	/** Drive the real frame-change path (per-frame sprite swap + per-child offset delta apply). */
	void Test_HandleFrameChanged(int32 NewFrame) { HandleFrameChanged(NewFrame); }
	/** The cached resolved profile entry (U4/U5 seam) — null when the current flipbook isn't in the profile. */
	const FFlipbookProfileEntry* Test_GetCachedProfileEntry() const { return GetCachedProfileEntry(); }
	/** Number of children carrying a tracked applied offset — 0 pins the zero-offset byte-identical no-op
	 *  (the apply branch never ran, so no AddWorldOffset call was ever made). */
	int32 Test_NumTrackedChildOffsets() const { return LastAppliedChildOffsetPx.Num(); }
	/** The tracked applied PIXEL offset for a layer's child (ZeroVector when untracked). Pixel-space since
	 *  the facing-flip fix — the world expression is derived under the CURRENT basis at each use. */
	FVector2D Test_GetLastAppliedChildOffsetPx(const FString& LayerName) const
	{
		const FVector2D* Found = LastAppliedChildOffsetPx.Find(LayerName);
		return Found ? *Found : FVector2D::ZeroVector;
	}

	// --- U7 one-time layer-sync-failure warning test seams ---
	/** Drive the BeginPlay binding-availability check worldlessly (BeginPlay asserts on unregistered components). */
	void Test_WarnIfLayerSyncBindingUnavailable() { WarnIfLayerSyncBindingUnavailable(); }
	/** True once the one-per-component sync-failure warning fired (pins fires-ONCE). */
	bool Test_HasWarnedLayerSyncFailure() const { return bLayerSyncFailureWarned; }
#endif

	// --- Transient generic selection preview API ---
	/** Preview one generic Layer activation without changing gameplay, persistence, or replication. */
	UFUNCTION(BlueprintCallable, Category = "Paper2D+|Appearance")
	bool PreviewLayerActive(FGuid LayerId, bool bActive);

	/** Preview one complete generic Appearance Preset without changing committed identity. */
	UFUNCTION(BlueprintCallable, Category = "Paper2D+|Appearance")
	bool PreviewAppearancePreset(FGuid PresetId);

	/** Promote the active preview to the committed selection (overlay → committed) and recompute. No-op if no preview. */
	UFUNCTION(BlueprintCallable, Category = "Paper2D+|Layers")
	void CommitPreview();

	/** Discard the active preview and re-resolve the committed view. No-op if no preview. */
	UFUNCTION(BlueprintCallable, Category = "Paper2D+|Layers")
	void CancelPreview();

	/** True while a try-before-equip preview is active (committed maps are untouched). */
	UFUNCTION(BlueprintCallable, Category = "Paper2D+|Layers")
	bool IsPreviewActive() const { return bPreviewActive; }

	/** Fired after a committed or preview selection is projected. Re-entrant broadcasts are suppressed. */
	UPROPERTY(BlueprintAssignable, Category = "Paper2D+|Layers")
	FPaper2DPlusOnLayersChanged OnLayersChanged;

	// ─── Networking opt-in (TASK-57 U6) ─────────────────────────────────
	// Appearance replication mirrors the profile-component pattern on a different component. COMMITTED
	// appearance descriptor replicates through the existing publish chokepoints; previews and transient recolor stay
	// local by construction (they never call the committed selection
	// publish sites). Single-player is byte-identical with bEnableReplication off — the publish is a no-op
	// unless Authority + bEnableReplication. There is no client→server appearance RPC: the game forwards
	// owner intent over its own channel and calls the setters ON AUTHORITY.

	/** Opt into server-authoritative appearance replication for this component. Default false —
	 *  single-player behavior is byte-identical with this off. MUST match between the server and client
	 *  archetypes. CONSUMED AT BEGINPLAY (SetIsReplicated runs there); flipping it at runtime is inert. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Networking")
	bool bEnableReplication = false;

	/** On a dedicated server (NM_DedicatedServer) with replication on, skip creating the child
	 *  UPaperSpriteComponents — a dedicated server renders nothing, so the visual layers are pure waste.
	 *  Authority appearance publish still works because PublishAppearanceStateIfAuthority reads the committed
	 *  descriptor (and logical visibility resolution is null-component-safe), never the child components (TASK-57 U6). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Networking")
	bool bSkipLayerComponentsOnDedicatedServer = true;

	/** The replicated COMMITTED appearance snapshot (TASK-57 U6) — resolver INPUTS only (never derived
	 *  visibility): clients re-run the single ResolveVisibleLayers chokepoint locally. Written ONLY by
	 *  PublishAppearanceStateIfAuthority on the authority; consumed via OnRep_AppearanceState on clients. */
	UPROPERTY(ReplicatedUsing = OnRep_AppearanceState)
	FPaper2DPlusRepAppearanceState RepAppearance;

	/** This component's resolved network context — its OWN copy of the U1/U2 seam (the profile component's
	 *  seams live on a different component and do NOT transfer). Standalone (replication off / no world /
	 *  NM_Standalone) = single-player identity: the publish is a no-op. */
	UFUNCTION(BlueprintPure, Category = "Networking")
	EPaper2DPlusNetContext GetNetContext() const;

	/** Replication registration (TASK-57 U6) — UNCONDITIONAL (registration is not replication: the
	 *  component only actually replicates when bEnableReplication opted in via SetIsReplicated at BeginPlay). */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Net-internal RepNotify for RepAppearance (TASK-57 U6): apply the replicated committed appearance by
	 *  re-running the SAME descriptor pipeline the setters use. Pre-BeginPlay / not-yet-ready component ⇒ stash into
	 *  PendingRepAppearance (drained at the BeginPlay tail); an ACTIVE preview ⇒ discard it WITHOUT re-applying
	 *  (CancelPreviewNoApply — no restore pass), then apply. Payload 2 restores the descriptor directly; payloads
	 *  Idempotent and same-sequence guarded.
	 *  PUBLIC for the worldless rigs (the repo's public-UFUNCTION testability pattern). */
	UFUNCTION()
	void OnRep_AppearanceState();

#if !UE_BUILD_SHIPPING
	// TEST SEAMS (TASK-57 U6) — plain C++ accessors over the replication internals so the worldless rigs can
	// force the net context (the profile component's seams don't transfer to this component), force the
	// dedicated-server skip branch, pre-write RepAppearance (net-driver simulation), and observe the stash.
	// Same non-UFUNCTION / Shipping-stripped rules as the U1/U2 seams.

	/** Force ResolveNetContext to return a fixed context (the override wins). Unset = normal resolution. */
	void SetNetContextOverrideForTests(TOptional<EPaper2DPlusNetContext> InOverride) { NetContextOverrideForTests = InOverride; }

	/** Force the dedicated-server skip branch in BeginPlay worldlessly (NM_DedicatedServer is unreachable
	 *  without a UWorld). When set, BeginPlay treats GetNetMode() == NM_DedicatedServer as this value. */
	void SetDedicatedServerOverrideForTests(bool bInIsDedicatedServer) { DedicatedServerOverrideForTests = bInIsDedicatedServer; }

	/** The replicated appearance snapshot — pre-write it (net-driver simulation) then drive OnRep_AppearanceState,
	 *  or observe the authority-side publishes (Sequence bump on a committed change, stable on a no-op). */
	FPaper2DPlusRepAppearanceState& GetRepAppearanceForTests() { return RepAppearance; }

	/** True while a deferred RepAppearance snapshot is stashed (pre-BeginPlay / component not ready). */
	bool HasPendingRepAppearanceForTests() const { return PendingRepAppearance.IsSet(); }

	/** Drive the BeginPlay-tail stash drain worldlessly (rigs can't run BeginPlay). */
	void DrainPendingRepAppearanceForTests() { DrainPendingRepAppearance(); }

	/** The exact predicate BeginPlay uses to skip child-component creation on a dedicated server (worldless rigs
	 *  can't run BeginPlay — UActorComponent::BeginPlay asserts on bRegistered — so the decision is tested directly). */
	bool WouldSkipLayerComponentsOnDedicatedServerForTests() const
	{
		return bSkipLayerComponentsOnDedicatedServer && IsDedicatedServerContext();
	}
	uint32 GetAppearanceGameplayOrderForTests() const { return LastAppearanceGameplayOrderForTests; }
	uint32 GetAppearanceVisualOrderForTests() const { return LastAppearanceVisualOrderForTests; }
	uint32 GetAppearancePublishOrderForTests() const { return LastAppearancePublishOrderForTests; }
	int32 GetLayerChildCountForTests() const { return LayerSpriteComponents.Num(); }
	uint64 GetAppearanceRequestSequenceForTests() const { return AppearanceRequestSequence; }
	const FPaper2DPlusAppearanceBudgetDecision& GetLastAppearanceDecisionForTests() const { return LastAppearanceDecision; }
	/** Strong consumer currently displayed after the production cache/handoff path. The harness deduplicates these
	 *  pointers before querying engine resource bytes; it never fabricates cache residency from a key formula. */
	const UPaper2DPlusAppearanceCompositeResource* GetActiveCompositeResourceForTests() const
	{
		return ActiveCompositeResource;
	}
	UPaper2DPlusAppearanceCompositeResource* GetPendingCompositeResourceForTests()
	{
		return PendingCompositeResource;
	}
	const UPaper2DPlusAppearanceCompositeResource* GetPendingCompositeResourceForTests() const
	{
		return PendingCompositeResource;
	}
	const FString& GetAppearanceCompositeErrorForTests() const { return AppearanceCompositeError; }
	bool IsAppearanceCompositeSupportedForTests() const { return bAppearanceCompositeSupported; }
	bool CanAdmitAppearanceCompositeForTests() const { return bAppearanceCanAdmitComposite; }
	bool IsAppearanceChangingForTests() const { return bAppearanceChanging; }
	EPaper2DPlusAppearanceTier GetPendingCompositeTierForTests() const { return PendingCompositeTier; }
	void SetHybridCompositeFrameStateForTests(
		UPaper2DPlusAppearanceCompositeResource* InActive,
		UPaper2DPlusAppearanceCompositeResource* InPending,
		bool bInAppearanceChanging)
	{
		ActiveCompositeResource = InActive;
		PendingCompositeResource = InPending;
		bAppearanceChanging = bInAppearanceChanging;
	}
	bool CanHandoffPendingCompositeAtFrameForTests(int32 FrameIndex) const
	{
		return CanHandoffPendingCompositeAtFrame(FrameIndex);
	}
	uint32 GetActiveCompositeFrameRefreshCountForTests() const
	{
		return ActiveCompositeFrameRefreshCountForTests;
	}
	void ApplyAppearanceBudgetDecisionForTests(const FPaper2DPlusAppearanceBudgetDecision& Decision)
	{
		ApplyAppearanceBudgetDecision(Decision);
	}
	uint32 GetAppearanceDecisionWorkCountForTests() const
	{
		return AppearanceDecisionWorkCountForTests;
	}
	uint32 GetAppearanceDecisionElisionCountForTests() const
	{
		return AppearanceDecisionElisionCountForTests;
	}
	void EnsureRuntimeLiveComponentsForTests(
		const TArray<FString>& RequiredLayerNames,
		int32 FrameIndex)
	{
		EnsureRuntimeLiveComponents(RequiredLayerNames, FrameIndex);
	}
	UPaperSprite* GetRuntimeLiveSpriteForTests(const FString& LayerName) const;
	uint32 GetLayerSpriteUpdatePassCountForTests() const
	{
		return LayerSpriteUpdatePassCountForTests;
	}
	uint32 GetLayerSpriteUpdateCountForTests(const FString& LayerName) const
	{
		const uint32* Count = LayerSpriteUpdateCountsForTests.Find(LayerName);
		return Count ? *Count : 0;
	}
	void ResetLayerSpriteUpdateCountsForTests()
	{
		LayerSpriteUpdatePassCountForTests = 0;
		LayerSpriteUpdateCountsForTests.Reset();
	}
#endif

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	/** Teardown belt for destructions that never run EndPlay (never-registered / worldless
	 *  DestroyComponent): pushes the SAME ClearAppearanceCombatDigest as EndPlay — idempotent, the normal
	 *  path's EndPlay clear already emptied the digest so this early-outs there (adversarial finding 1a). */
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

private:
	UPROPERTY()
	TMap<FString, TObjectPtr<UPaperSpriteComponent>> LayerSpriteComponents;

	// --- U30 hybrid runtime presentation (client-local, transient, never replicated/serialized) ---
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> CompositePrimitive = nullptr;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> CompositeMID = nullptr;
	UPROPERTY(Transient)
	TObjectPtr<UPaper2DPlusAppearanceCompositeResource> PendingCompositeResource = nullptr;
	UPROPERTY(Transient)
	TObjectPtr<UPaper2DPlusAppearanceCompositeResource> ActiveCompositeResource = nullptr;
	/** Constructor-resolved hard references; build/frame paths never load. */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> CompositePlaneMesh = nullptr;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> CompositeDisplayMaterial = nullptr;
	TWeakObjectPtr<UPaper2DPlusAppearanceBudgetSubsystem> AppearanceBudgetSubsystem;
	FPaper2DPlusAppearanceRenderPolicy AppearanceRenderPolicy;
	FPaper2DPlusAppearanceBudgetDecision LastAppearanceDecision;
	/** Last decision whose visual/build work was actually projected. LastAppearanceDecision remains the current
	 *  every-frame diagnostic value even when an identical steady decision is safely elided. */
	FPaper2DPlusAppearanceBudgetDecision LastAppliedAppearanceDecision;
	FPaper2DPlusAppearanceDescriptor VisualAppearance;
	TArray<FString> VisualLayerNames;
	TArray<FString> PendingIndependentChannelNames;
	TArray<FString> ActiveIndependentChannelNames;
	FString PendingCompositeKeyLabel;
	FString AppearanceCompositeError;
	uint64 AppearanceRegistrationId = 0;
	uint64 AppearanceRequestSequence = 0;
	uint64 LastObservedCacheMutationSerial = 0;
	int32 AppearancePriorityOverride = 0;
	int32 LastObservedFrameIndex = 0;
	EPaper2DPlusAppearanceTier PendingCompositeTier = EPaper2DPlusAppearanceTier::FarComposite;
	EPaper2DPlusAppearanceTier ActiveCompositeTier = EPaper2DPlusAppearanceTier::DescriptorOnly;
	bool bAppearanceCompositeSupported = false;
	bool bAppearanceCanAdmitComposite = true;
	bool bPendingWasCacheHit = false;
	bool bAppearanceChanging = false;
	bool bHasAppliedAppearanceDecision = false;

	/** The currently applied visibility per Layer
	 * and used as the initial visibility when CreateLayerComponents rebuilds). */
	TMap<FString, bool> LayerVisibilityState;

	/** Sole committed selection authority for gameplay, persistence, replication, and presentation. */
	UPROPERTY(Transient)
	FPaper2DPlusAppearanceDescriptor CommittedAppearance;
	bool bHasCommittedAppearance = false;
	/** Re-entry guard so an OnLayersChanged handler that calls back into the swap API cannot recurse forever. */
	bool bBroadcastingLayersChanged = false;

	/** Transient generic candidate descriptor. */
	TOptional<FPaper2DPlusAppearanceDescriptor> PreviewAppearance;
	/** True while a preview overlay is active. */
	bool bPreviewActive = false;

	FString CachedAnimationName;

	// --- Per-child sprite offsets (layered-asset redesign U4, D1 runtime parity) ---
	// The resolved BaseProfile entry for the CURRENT flipbook, cached on flipbook change (HandleFlipbookChanged
	// used to discard FlipbookIdx). Stored as WEAK PROFILE + INDEX — never a raw FFlipbookProfileEntry* — and
	// re-resolved per use with an IsValidIndex guard, so a Flipbooks[] realloc/shrink can't dangle it (the
	// shared-cache raw-pointer hazard). U5's committed combat compose will reuse this seam.
	TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> CachedEntryProfile;
	int32 CachedFlipbookEntryIndex = INDEX_NONE;
	/** Re-resolve the cached entry (null when unset / profile gone / index out of range). */
	const FFlipbookProfileEntry* GetCachedProfileEntry() const;

	/** LayerName -> the PIXEL offset currently applied to that layer's child (the per-CHILD LastApplied
	 *  tracker — offset doc recipe, multiplied per child). Stored in PIXEL space, NOT as the applied world
	 *  vector (adversarial finding 2): the applied offset lives in the child's RELATIVE transform, so when
	 *  the actor mirror-flips (yaw-180 / negative scale.X) between applications the already-applied vector
	 *  ROTATES WITH the flip — a stored world vector then subtracts the pre-flip direction and misapplies
	 *  by (R−I)*Last permanently. Converting the stored px under the CURRENT child basis at each use makes
	 *  the undo term equal the rotated applied vector (mirror flips commute through PixelOffsetToWorld:
	 *  the conversion's facing sign flip IS the world-X mirror the parent flip applies).
	 *  A layer absent from the map has NO offset applied (zero-offset assets never populate it, so they
	 *  stay byte-identical — no AddWorldOffset call is made). Cleared by DestroyLayerComponents (children
	 *  die with their offsets) and by UndoAppliedChildOffsets. */
	TMap<FString, FVector2D> LastAppliedChildOffsetPx;

	/** Compute + delta-apply each child's world offset for a key frame: TotalOffsetPx via the SHARED
	 *  Paper2DPlusLayerDraw home; delta = W(NewPx, basis) − W(LastPx, basis) with BOTH terms converted
	 *  under the CHILD's CURRENT scale/facing/PPU (the finding-2 mirror-commutation rule — see the
	 *  LastAppliedChildOffsetPx comment), AddWorldOffset(delta) — NEVER SetRelativeLocation (offset doc).
	 *  Called at the UpdateLayerSprites tail (covers frame change AND the flipbook-change frame-0 apply).
	 *  Missing children / dedicated server: guarded no-ops. */
	void ApplyChildOffsetsForFrame(int32 FrameIndex);

	/** Undo every applied child offset (AddWorldOffset(−W(LastPx, CURRENT basis)) — the current-basis
	 *  conversion equals the flip-rotated applied vector, finding 2) and clear the tracker — the
	 *  flipbook-change cleanup that runs BEFORE sprites are swapped / the new animation is warmed. */
	void UndoAppliedChildOffsets();

	// --- Networking internals (TASK-57 U6) ---
	/** Snapshot of bEnableReplication taken at BeginPlay (the flag is CONSUMED there — SetIsReplicated runs once).
	 *  GetNetContext honors this snapshot post-BeginPlay so a runtime flip can't retroactively change registration. */
	bool bReplicationEnabledAtBeginPlay = false;
	/** True once appearance application is ready. Legacy sets it when child sprite components exist; Runtime
	 *  Customizable and Fixed/Baked set it without children. OnRep stashes until this is true, then the BeginPlay
	 *  tail drains the latest snapshot. A dedicated-server authority never receives OnReps. */
	bool bLayerComponentsReady = false;
	/** Warn once when a replicated appearance snapshot arrives while replication is inactive locally
	 *  (server/client archetype mismatch) — mirrors the profile component's bWarnedArchetypeMismatch so the
	 *  appearance network path is just as diagnosable. Transient. */
	bool bWarnedAppearanceArchetypeMismatch = false;
	/** Warn once when a SUPPORTED replicated appearance snapshot is rejected by the local apply
	 *  (server/client Layer-asset skew) — paired with committing the rejected Sequence so the same
	 *  snapshot is not re-admitted on every re-received bunch. Transient. */
	bool bWarnedAppearanceApplySkew = false;
	/** Warn once when a committed appearance setter is called on a non-authority networked context —
	 *  the call is refused (server-authoritative; the committed result replicates down). Transient. */
	bool bWarnedAppearanceProxyMutation = false;
	/** True while OnRep/drain applies a replicated snapshot — the commit chokepoint's authority gate
	 *  must pass on proxies for exactly this path. */
	bool bApplyingReplicatedAppearance = false;
	/** True while BeginPlay seeds the dress-on-spawn default — proxy-allowed by design (the drained
	 *  server snapshot authoritatively replaces the local default right after). */
	bool bApplyingStartupAppearance = false;
	/** A committed apply ran while ProfileComponent was unbound, so the gameplay-composition push was
	 *  skipped — the BeginPlay tail re-drives it once (the spawn-order window). */
	bool bAppearanceCombatPushDeferred = false;
	/** A replicated snapshot that arrived before appearance application was ready (pre-BeginPlay) — stashed here
	 *  and applied by DrainPendingRepAppearance once ready. Latest-wins. */
	TOptional<FPaper2DPlusRepAppearanceState> PendingRepAppearance;
	/** Same-sequence guard for OnRep/stash drains. 0 is the never-applied sentinel. */
	uint16 LastAppliedAppearanceSequence = 0;
#if !UE_BUILD_SHIPPING
	/** Test-only forced net context (the worldless rigs can't reach a real net role). */
	TOptional<EPaper2DPlusNetContext> NetContextOverrideForTests;
	/** Test-only forced dedicated-server verdict (NM_DedicatedServer is unreachable worldless). */
	TOptional<bool> DedicatedServerOverrideForTests;
	uint32 AppearancePipelineCounterForTests = 0;
	uint32 LastAppearanceGameplayOrderForTests = 0;
	uint32 LastAppearanceVisualOrderForTests = 0;
	uint32 LastAppearancePublishOrderForTests = 0;
	uint32 LayersChangedBroadcastCountForTests = 0;
	uint32 ActiveCompositeFrameRefreshCountForTests = 0;
	uint32 AppearanceDecisionWorkCountForTests = 0;
	uint32 AppearanceDecisionElisionCountForTests = 0;
	uint32 LayerSpriteUpdatePassCountForTests = 0;
	TMap<FString, uint32> LayerSpriteUpdateCountsForTests;
#endif

	/** This component's resolved net context (the body of GetNetContext; mirrors the U2 profile-component seam). */
	EPaper2DPlusNetContext ResolveNetContext() const;
	/** True on a dedicated server (or the test override) — gates the layer-component-creation skip. */
	bool IsDedicatedServerContext() const;
	/** Publish the committed appearance snapshot into RepAppearance — Authority + bEnableReplication only, no-op otherwise.
	 *  Compares semantic descriptor equality ignoring Sequence and bumps Sequence
	 *  only on a real delta (a no-op republish must never stomp a client-local preview). Called ONLY at the tail of
	 *  CommitAppearanceState; previews/manual-visibility/recolor bypass it by construction. */
	void PublishAppearanceStateIfAuthority();
	/** Apply a replicated committed snapshot by re-running the SAME resolver the setters use (resolver INPUTS are
	 *  replicated, visibility is re-derived locally — never replicate derived visibility). Discards an active preview
	 *  WITHOUT a restore pass first (CancelPreviewNoApply). Idempotent. Runs only on proxies (OnRep never on authority). */
	bool ApplyReplicatedAppearance(const FPaper2DPlusRepAppearanceState& Snapshot);
	/** Discard an active preview overlay without a restore/recompute pass — the OnRep
	 *  apply that calls this is about to set committed visibility itself, so a restore here would be a wasted stomped pass. */
	void CancelPreviewNoApply();
	/** Drain a stashed PendingRepAppearance once appearance application is ready (BeginPlay tail). */
	void DrainPendingRepAppearance();

	/** Reject Fixed/Baked mutations through one guarded diagnostic seam. */
	bool CanMutateRuntimeAppearance(bool bEmitDiagnostic = true);

	/** Authority gate at the commit chokepoint: proxies may not mutate committed appearance (warn-once
	 *  + refuse) — bypassed while applying replicated state or the BeginPlay startup seed. */
	bool CanCommitAppearanceMutation();

	/** Commit the Sequence of a rejected-but-supported replicated snapshot (asset skew) so it is not
	 *  re-attempted every bunch, and warn once — mirrors OnRep_AnimState's skew hardening. */
	void CommitRejectedAppearanceSequence(uint16 Sequence);

	/** Re-drive the deferred gameplay-composition push once ProfileComponent exists (BeginPlay tail). */
	void RedrivePendingAppearanceCombatPush();

	/** Descriptor-first committed pipeline: descriptor -> gameplay -> one visual advisory -> authority publish. */
	void CommitAppearanceState();
	/** Commit an already-validated generic descriptor through the same ordered authority pipeline. */
	bool CommitGenericAppearanceState(const FPaper2DPlusAppearanceDescriptor& Appearance);
	/** Apply the current CommittedAppearance to gameplay, visuals, and optional replication in that order. */
	void ApplyCommittedAppearanceState();

	/** Bind only the listeners required by the asset's exclusive delivery mode. Runtime Customizable observes
	 *  flipbook identity and frame changes; Fixed/Baked observes neither. */
	void BindAnimationListeners();
	/** Resolve the current authored animation identity. Uses the assigned/loaded profile first; the only possible
	 *  synchronous fallback is a BaseProfile load at a flipbook boundary, never during a frame update. */
	FString ResolveAnimationNameAtBoundary(UPaperFlipbook* NewFlipbook) const;

	void RegisterAppearanceScheduler();
	void UnregisterAppearanceScheduler();
	void SubmitAppearanceBudgetRequest();
	void ApplyAppearanceBudgetDecision(const FPaper2DPlusAppearanceBudgetDecision& Decision);
	void RequestHybridAppearance(bool bLogicalChange);
	bool PrepareCompositeForTier(EPaper2DPlusAppearanceTier Tier);
	bool BuildCompositeRecipe(
		EPaper2DPlusAppearanceTier Tier,
		FPaper2DPlusAppearanceBuildRecipe& OutRecipe,
		FString& OutError);
	void BuildGrantedCompositeFrames(int32 GrantedUnits);
	bool CanHandoffPendingCompositeAtFrame(int32 FrameIndex) const;
	void TryHybridHandoffAtFrame(int32 FrameIndex);
	void ApplyHybridTierVisuals(EPaper2DPlusAppearanceTier Tier, int32 FrameIndex);
	void EnsureRuntimeLiveComponents(const TArray<FString>& RequiredLayerNames, int32 FrameIndex);
	void DestroyCompositePrimitive();
	void ReleaseHybridVisuals();
	int32 GetCurrentKeyFrameIndex() const;
	bool IsHybridRuntimeActive() const;
	void CacheRuntimeAnimation(UPaperFlipbook* NewFlipbook);

	/** Hide every live Layer sprite child without changing the descriptor/logical visibility. */
	void HideLiveLayerChildren();

	// --- Per-layer recolor / dye (WS4-4A) ---
	// TRANSIENT — non-serialized presentation state keyed by Layer display name for the live component map.
	// UPROPERTY(Transient) (NOT plain): the
	// `Transient` flag keeps it OUT of serialization (so there is no on-disk recolor field / no schema bump), while the
	// reflection still GC-roots the FPaper2DPlusRecolorState::PaletteLUT TObjectPtr so a picked LUT can't be collected
	// before ApplyRecolor pushes it onto the (also-rooted) MID.
	/** LayerName -> desired recolor. A layer absent from this map keeps its stock material. */
	UPROPERTY(Transient)
	TMap<FString, FPaper2DPlusRecolorState> RecolorState;
	/** LayerName -> the cached MID currently driving that layer's slot-0 material. Created lazily by ApplyRecolor,
	 *  reused across recomputes (idempotent — no per-recompute re-creation), and emptied in DestroyLayerComponents /
	 *  RefreshLayers (its components are gone). UPROPERTY so the MIDs are GC-rooted while live. */
	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UMaterialInstanceDynamic>> LayerMIDs;

	/** Hard references to the current animation's Layer sprites, refreshed at animation boundaries.
	 *  WarmLayerSprites loads them once so per-frame reads are
	 *  load-free (.Get()), but a TSoftObjectPtr does NOT root its target — without this UPROPERTY a GC between
	 *  warm and a not-yet-displayed frame's read would collect the sprite and the layer would render blank.
	 *  Cleared+refilled per animation so prior animations' sprites become GC-eligible again (no leak). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UPaperSprite>> WarmedLayerSprites;

	void CreateLayerComponents();
	void DestroyLayerComponents();

	/**
	 * POST-resolve recolor pass. Called at the END of the three visibility/frame mutation sites (RecomputeVisibilityUsing,
	 * generic visibility resolution and UpdateLayerSprites — NEVER inside ResolveVisibleLayers. For each Layer with a sprite
	 * component: if it is VISIBLE and has a RecolorState entry and RecolorBaseMaterial is non-null, lazily get-or-create
	 * the layer's MID, push the Tier-1 params, and set it on slot 0; otherwise (no entry, hidden, or null base material)
	 * if the layer currently has a MID, restore the stock material and drop the MID. A layer that never had a MID is left
	 * completely untouched, so a clean checkout's OverrideMaterials array stays empty (byte-identical no-op).
	 */
	void ApplyRecolor();

	UFUNCTION()
	void HandleFrameChanged(int32 NewFrameIndex);

	UFUNCTION()
	void HandleFlipbookChanged(UPaperFlipbook* NewFlipbook);

	// --- One-time layer-sync failure warning (layered-asset redesign U7, R37 drive-by) ---
	// Layer sync fails SILENTLY today in two shapes: (a) the actor's flipbook component is a stock
	// UPaperFlipbookComponent (or none/not yet resolved) so the frame/flipbook delegates never bind at
	// BeginPlay, and (b) the layer asset's BaseProfile soft ref is unset or fails to load so
	// HandleFlipbookChanged can never resolve an animation. Either way the layers freeze with no clue.
	// Emit ONE Warning per component naming the owner + cause; never spam per-frame paths.
	/** True once the one-per-component warning fired (first failure wins; later causes stay silent). */
	bool bLayerSyncFailureWarned = false;
	/** Emit the one-per-component sync-failure warning (no-op once fired). */
	void WarnLayerSyncFailureOnce(const FString& Cause);
	/** BeginPlay tail: warn when the frame/flipbook delegate binding was NOT made (stock component / no
	 *  component / not-yet-resolved profile field) — the cause is named via GetResolvedFlipbookComponent. */
	void WarnIfLayerSyncBindingUnavailable();

	void UpdateLayerSprites(int32 FrameIndex);
	/** Project the committed descriptor to the current animation and visual primitives. */
	void RecomputeVisibility();
	/** Generic descriptor visual projection. Selection remains global; only authored animation rows contribute. */
	void RecomputeVisibilityUsingDescriptor(const FPaper2DPlusAppearanceDescriptor& Appearance);

	/** Recompute through the preview descriptor when active, otherwise the committed descriptor. */
	void RecomputeVisibilityActive();

	/** Fires OnLayersChanged once, guarded against re-entrant broadcasts. */
	void BroadcastLayersChanged();
};
