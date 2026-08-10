// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Paper2DPlusTypes.h"
#include "UObject/SoftObjectPath.h"
#include "Paper2DPlusHitboxSubsystem.generated.h"

class UPaper2DPlusCharacterProfileComponent;
class UPaper2DPlusClashGraphAsset;
struct FClashGraph;

/**
 * Lazy broadphase for Paper2DPlus runtime hit checks.
 *
 * Profile components register at BeginPlay. Queries rebuild a per-frame spatial
 * index of registered hurtboxes once, then active attackers test only nearby
 * candidates instead of doing actor-vs-actor pair scans.
 *
 * TASK-145: tickable ONLY while auto-hit-detection components are armed (attack frames on screen) —
 * the armed-set tick re-checks overlaps BETWEEN key-frame transitions so movement during a held
 * active frame still connects. IsTickable() is false whenever the armed set is empty, so a world
 * with nobody mid-attack pays zero tick cost.
 */
UCLASS()
class PAPER2DPLUS_API UPaper2DPlusHitboxSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Hitboxes")
	void RegisterProfileComponent(UPaper2DPlusCharacterProfileComponent* Component);

	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Hitboxes")
	void UnregisterProfileComponent(UPaper2DPlusCharacterProfileComponent* Component);

	/** TASK-145: components currently ARMED for automatic hit detection register here (the profile
	 *  component's arming edges are the only callers). Weak-held — a dying component can never
	 *  strand the subsystem ticking. */
	void RegisterArmedAutoDetect(UPaper2DPlusCharacterProfileComponent* Component);
	void UnregisterArmedAutoDetect(UPaper2DPlusCharacterProfileComponent* Component);

	// FTickableGameObject (via UTickableWorldSubsystem) — conditional: ticks only while armed
	// auto-detect components exist (TASK-145).
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual TStatId GetStatId() const override;

	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Hitboxes")
	bool QueryAttackOverlaps(AActor* Attacker, TArray<FHitboxCollisionResult>& OutResults);

	bool QueryAttackOverlaps(UPaper2DPlusCharacterProfileComponent* AttackerComponent, TArray<FHitboxCollisionResult>& OutResults);

	/** TASK-91 (Codex F197a): explicit-boxes overload — sweeps the GIVEN attack boxes for the attacker
	 *  instead of the component's CURRENT-frame cached boxes, so span-based hit validation can sample
	 *  the RESOLVED in-span attack frame's geometry (a coarse server tick that crossed a 1-frame active
	 *  window and settled on a non-attack frame). Same broadphase/narrowphase/clash sweep as the cached
	 *  overload, which now delegates here. */
	bool QueryAttackOverlaps(UPaper2DPlusCharacterProfileComponent* AttackerComponent, const TArray<FWorldHitbox>& AttackBoxes, TArray<FHitboxCollisionResult>& OutResults);

	/** TASK-77 U4: ADVISORY attack-vs-attack clash query (gate+advise) — resolves the attacker's attack boxes
	 *  against OTHER actors' attack boxes via the project clash graph. SEPARATE from QueryAttackOverlaps (the
	 *  authoritative hurtbox oracle is untouched). Empty/no clash graph -> no results (zero-cost). */
	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Hitboxes")
	bool QueryAttackClashes(AActor* Attacker, TArray<FHitboxClashResult>& OutResults);

	bool QueryAttackClashes(UPaper2DPlusCharacterProfileComponent* AttackerComponent, TArray<FHitboxClashResult>& OutResults);

	/** Pure worldless overlap test shared by runtime queries and editor previews. */
	static bool IntersectHitboxes2D(const FWorldHitbox& A, const FWorldHitbox& B);

	void MarkIndexDirty();

private:
	struct FIndexedHurtbox
	{
		TWeakObjectPtr<UPaper2DPlusCharacterProfileComponent> Component;
		FWorldHitbox Hurtbox;
	};

	TSet<TWeakObjectPtr<UPaper2DPlusCharacterProfileComponent>> RegisteredComponents;
	TArray<FIndexedHurtbox> IndexedHurtboxes;
	TMap<FIntPoint, TArray<int32>> HurtboxCells;

	/** TASK-145: the armed auto-detect set — drives IsTickable()/Tick(). Mutated only through
	 *  Register/UnregisterArmedAutoDetect and the Tick stale-sweep. */
	TSet<TWeakObjectPtr<UPaper2DPlusCharacterProfileComponent>> ArmedAutoDetectComponents;

	/** TASK-77 U4: a PARALLEL attack-box index, populated only when a non-empty clash graph is assigned (so
	 *  it stays empty + zero-cost for every project not using attack-vs-attack clash). Shares the one
	 *  BuiltFrameCounter/bIndexDirty rebuild gate with the hurtbox index. Same FIndexedHurtbox shape (it's
	 *  just (Component, FWorldHitbox) — the box carries its ClashCategory). */
	TArray<FIndexedHurtbox> IndexedAttackboxes;
	TMap<FIntPoint, TArray<int32>> AttackboxCells;

	/** TASK-77 U4: set the first time QueryAttackClashes is called, so a project that uses ONLY the U3
	 *  defensive gate (which also needs a non-empty clash graph) never pays for the attack-box index it
	 *  doesn't query. The index is built only when this is true AND a non-empty graph is assigned. */
	bool bAttackClashUsed = false;

	/** Audit F3: the flipbook-component transform each indexed component was built at. A defender that
	 *  MOVES after the per-frame index built (same GFrameCounter) makes BOTH the spatial cells and the
	 *  hurtbox snapshots stale — a narrowphase-only refresh wouldn't gather a defender that moved INTO
	 *  range — so a drift here forces a full rebuild before the next query. Matches the per-component
	 *  freshness model (RefreshCachedWorldState keys on the same flipbook transform). (Merged with TASK-77
	 *  U4: a component contributing only ATTACK boxes is tracked too — the attack index goes stale the
	 *  same way.) */
	TMap<TWeakObjectPtr<UPaper2DPlusCharacterProfileComponent>, FTransform> IndexedComponentTransforms;

	uint64 BuiltFrameCounter = MAX_uint64;
	bool bIndexDirty = true;
	float CellSize = 500.0f;

	/** TASK-77 U3: the resolved active clash graph asset, PINNED (UPROPERTY roots it) so a GC can't evict it
	 *  between combat frames and force a synchronous re-load on the authoritative hit path. Re-resolved only
	 *  when the project's DefaultClashGraph soft path changes — see GetActiveClashGraph. */
	UPROPERTY(Transient)
	TObjectPtr<UPaper2DPlusClashGraphAsset> CachedClashGraphAsset = nullptr;
	FSoftObjectPath CachedClashGraphPath;
	/** The active clash graph by const-ref (no copy); loads + pins on a settings change, else a cheap return. */
	const FClashGraph& GetActiveClashGraph();

	void BuildIndexIfNeeded();
	void BuildIndex();
	/** Audit F3: true when any indexed component vanished or moved since BuildIndex recorded it. */
	bool HasIndexedComponentTransformDrift() const;
	/** The flipbook-component transform that drives a component's world hurtboxes (FTransform::Identity if
	 *  unresolved) — the SAME transform RefreshCachedWorldState keys its cache on. */
	static FTransform ResolveIndexTransform(const UPaper2DPlusCharacterProfileComponent* Component);
	/** TASK-77 U4: parameterized over the target cell map (same body) so the hurtbox AND attack indices share
	 *  one implementation. The hurtbox call sites pass HurtboxCells; the attack sites pass AttackboxCells. */
	void AddBoxToCells(int32 BoxIndex, const FWorldHitbox& Box, TMap<FIntPoint, TArray<int32>>& Cells);
	void GatherCandidates(const FWorldHitbox& QueryBox, const TMap<FIntPoint, TArray<int32>>& Cells, TSet<int32>& OutCandidateIndices) const;

	FIntPoint GetCellForPoint(const FVector2D& Point) const;
	static FBox2D GetHitboxBounds2D(const FWorldHitbox& Hitbox);

#if !UE_BUILD_SHIPPING
public:
	/** Test seam (F3): how many times BuildIndex actually rebuilt the index — lets a test assert that a
	 *  query with no transform/frame change does NOT rebuild needlessly, and that a same-frame move does. */
	int32 Test_IndexRebuildCount = 0;
	void Test_BuildIndexIfNeeded() { BuildIndexIfNeeded(); }
	/** Test seam (TASK-145): the armed auto-detect count (also the IsTickable() condition). */
	int32 Test_ArmedAutoDetectCount() const { return ArmedAutoDetectComponents.Num(); }
#endif
};
