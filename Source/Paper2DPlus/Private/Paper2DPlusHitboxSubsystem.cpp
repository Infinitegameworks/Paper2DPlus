// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusHitboxSubsystem.h"
#include "Paper2DPlusCharacterProfileComponent.h"
#include "Paper2DPlusClash.h"            // AttackConnects — the clash keep/suppress decision (TASK-77 U3)
#include "Paper2DPlusClashGraphAsset.h"  // the active clash graph
#include "Paper2DPlusSettings.h"         // UPaper2DPlusSettings::DefaultClashGraph
#include "PaperFlipbookComponent.h" // GetComponentTransform on the resolved flipbook comp (audit F3)
#include "CoreGlobals.h"
#include "GameFramework/Actor.h"

void UPaper2DPlusHitboxSubsystem::RegisterProfileComponent(UPaper2DPlusCharacterProfileComponent* Component)
{
	if (!IsValid(Component)) return;

	RegisteredComponents.Add(Component);
	MarkIndexDirty();
}

void UPaper2DPlusHitboxSubsystem::UnregisterProfileComponent(UPaper2DPlusCharacterProfileComponent* Component)
{
	if (!Component) return;

	RegisteredComponents.Remove(Component);
	ArmedAutoDetectComponents.Remove(Component); // a full unregister always drops the armed slot too
	MarkIndexDirty();
}

// ─── Automatic hit detection armed set (TASK-145) ───────────────────────

void UPaper2DPlusHitboxSubsystem::RegisterArmedAutoDetect(UPaper2DPlusCharacterProfileComponent* Component)
{
	if (!IsValid(Component)) return;

	ArmedAutoDetectComponents.Add(Component);
}

void UPaper2DPlusHitboxSubsystem::UnregisterArmedAutoDetect(UPaper2DPlusCharacterProfileComponent* Component)
{
	if (!Component) return;

	ArmedAutoDetectComponents.Remove(Component);
}

bool UPaper2DPlusHitboxSubsystem::IsTickable() const
{
	// Conditional tickable: the subsystem costs nothing unless someone is mid-attack (TASK-145).
	return ArmedAutoDetectComponents.Num() > 0;
}

TStatId UPaper2DPlusHitboxSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UPaper2DPlusHitboxSubsystem, STATGROUP_Tickables);
}

void UPaper2DPlusHitboxSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (ArmedAutoDetectComponents.Num() == 0)
	{
		return;
	}

	// Snapshot: a component's pass can disarm it (or others) mid-iteration, mutating the set.
	// Tickables run AFTER the frame's actor ticks, so this pass sees the frame's movement — the
	// mid-hold re-check that frame-entry detection alone would miss (TASK-145). Repeat passes are
	// dedup-safe by construction (the component's ProcessedHits ledger).
	TArray<TWeakObjectPtr<UPaper2DPlusCharacterProfileComponent>> Snapshot = ArmedAutoDetectComponents.Array();
	for (const TWeakObjectPtr<UPaper2DPlusCharacterProfileComponent>& WeakComponent : Snapshot)
	{
		UPaper2DPlusCharacterProfileComponent* Component = WeakComponent.Get();
		if (!IsValid(Component))
		{
			ArmedAutoDetectComponents.Remove(WeakComponent);
			continue;
		}
		Component->TickAutoHitDetection();
	}
}

bool UPaper2DPlusHitboxSubsystem::QueryAttackOverlaps(AActor* Attacker, TArray<FHitboxCollisionResult>& OutResults)
{
	OutResults.Empty();
	if (!IsValid(Attacker)) return false;

	UPaper2DPlusCharacterProfileComponent* AttackerComponent = Attacker->FindComponentByClass<UPaper2DPlusCharacterProfileComponent>();
	return QueryAttackOverlaps(AttackerComponent, OutResults);
}

bool UPaper2DPlusHitboxSubsystem::QueryAttackOverlaps(UPaper2DPlusCharacterProfileComponent* AttackerComponent, TArray<FHitboxCollisionResult>& OutResults)
{
	OutResults.Empty();
	if (!IsValid(AttackerComponent)) return false;

	// Cached-box form: the attacker's CURRENT-frame world attack boxes, then the shared sweep below.
	TArray<FWorldHitbox> AttackBoxes;
	if (!AttackerComponent->GetCachedWorldAttackBoxes(AttackBoxes))
	{
		return false;
	}
	return QueryAttackOverlaps(AttackerComponent, AttackBoxes, OutResults);
}

bool UPaper2DPlusHitboxSubsystem::QueryAttackOverlaps(UPaper2DPlusCharacterProfileComponent* AttackerComponent, const TArray<FWorldHitbox>& AttackBoxes, TArray<FHitboxCollisionResult>& OutResults)
{
	// TASK-91 (Codex F197a): the explicit-boxes sweep — identical to the historical cached-box body
	// except the boxes come from the caller (the span validator passes the RESOLVED attack frame's
	// boxes; the cached overload above passes the current frame's).
	OutResults.Empty();
	if (!IsValid(AttackerComponent)) return false;

	BuildIndexIfNeeded();

	AActor* AttackerOwner = AttackerComponent->GetOwner();
	TSet<int32> CandidateIndices;

	// TASK-77 U3: the active clash graph (by const-ref, no copy), pinned + loaded only on a settings change so
	// the authoritative hit path never re-syncs per query. Empty (shipped DefaultClashGraph=None) -> no-op.
	const FClashGraph& ClashGraph = GetActiveClashGraph();

	for (const FWorldHitbox& AttackBox : AttackBoxes)
	{
		CandidateIndices.Reset();
		GatherCandidates(AttackBox, HurtboxCells, CandidateIndices);

		for (int32 CandidateIndex : CandidateIndices)
		{
			if (!IndexedHurtboxes.IsValidIndex(CandidateIndex))
			{
				continue;
			}

			const FIndexedHurtbox& Candidate = IndexedHurtboxes[CandidateIndex];
			UPaper2DPlusCharacterProfileComponent* DefenderComponent = Candidate.Component.Get();
			if (!IsValid(DefenderComponent) || DefenderComponent == AttackerComponent)
			{
				continue;
			}

			AActor* DefenderOwner = DefenderComponent->GetOwner();
			if (!IsValid(DefenderOwner) || DefenderOwner == AttackerOwner)
			{
				continue;
			}

			if (!IntersectHitboxes2D(AttackBox, Candidate.Hurtbox))
			{
				continue;
			}

			// TASK-77 U3: a spatial overlap is confirmed — does the attack CONNECT, or does the defender's
			// frame DefenseClass (Armor/Parry/Invincible) beat it? Suppress (drop the result) only when the
			// defense wins. Provable no-op for an untagged defender (the common case) — AttackConnects fast-
			// paths to true. The server re-derives this same verdict via the same query (ValidateMoveHit parity).
			if (!Paper2DPlusClash::AttackConnects(AttackBox.ClashCategory, Candidate.Hurtbox.DefenseClass, ClashGraph))
			{
				continue;
			}

			FHitboxCollisionResult Result;
			Result.bHit = true;
			Result.AttackBox = AttackBox;
			Result.HurtBox = Candidate.Hurtbox;
			Result.DefenderActor = DefenderOwner;
			Result.Damage = AttackBox.Damage;
			Result.Knockback = AttackBox.Knockback;

			const FBox2D AttackBounds = GetHitboxBounds2D(AttackBox);
			const FBox2D HurtBounds = GetHitboxBounds2D(Candidate.Hurtbox);
			const FBox2D Overlap(
				FVector2D(FMath::Max(AttackBounds.Min.X, HurtBounds.Min.X), FMath::Max(AttackBounds.Min.Y, HurtBounds.Min.Y)),
				FVector2D(FMath::Min(AttackBounds.Max.X, HurtBounds.Max.X), FMath::Min(AttackBounds.Max.Y, HurtBounds.Max.Y)));
			Result.HitLocation = Overlap.GetCenter();

			OutResults.Add(Result);
		}
	}

	return OutResults.Num() > 0;
}

const FClashGraph& UPaper2DPlusHitboxSubsystem::GetActiveClashGraph()
{
	// A named static empty graph (lvalue — never dangles) is the fallback when no clash graph is assigned
	// (the shipped DefaultClashGraph=None). Re-resolve (LoadSynchronous + cache) ONLY when the project's
	// DefaultClashGraph soft path changes — the cached asset is a UPROPERTY, so once loaded it is pinned
	// against GC and the authoritative hit path never re-syncs per query.
	static const FClashGraph EmptyClashGraph;
	const UPaper2DPlusSettings* Settings = UPaper2DPlusSettings::Get();
	const FSoftObjectPath Path = Settings ? Settings->DefaultClashGraph.ToSoftObjectPath() : FSoftObjectPath();
	if (Path != CachedClashGraphPath)
	{
		CachedClashGraphPath = Path;
		CachedClashGraphAsset = (Settings && Path.IsValid()) ? Settings->DefaultClashGraph.LoadSynchronous() : nullptr;
	}
	return CachedClashGraphAsset ? CachedClashGraphAsset->Graph : EmptyClashGraph;
}

bool UPaper2DPlusHitboxSubsystem::QueryAttackClashes(AActor* Attacker, TArray<FHitboxClashResult>& OutResults)
{
	OutResults.Empty();
	if (!IsValid(Attacker)) return false;

	UPaper2DPlusCharacterProfileComponent* AttackerComponent = Attacker->FindComponentByClass<UPaper2DPlusCharacterProfileComponent>();
	return QueryAttackClashes(AttackerComponent, OutResults);
}

bool UPaper2DPlusHitboxSubsystem::QueryAttackClashes(UPaper2DPlusCharacterProfileComponent* AttackerComponent, TArray<FHitboxClashResult>& OutResults)
{
	OutResults.Empty();
	if (!IsValid(AttackerComponent)) return false;

	// First use opts this world into building the attack-box index — force a rebuild so THIS call sees it
	// (a same-frame QueryAttackOverlaps may already have built the index without the attack boxes).
	if (!bAttackClashUsed)
	{
		bAttackClashUsed = true;
		MarkIndexDirty();
	}
	BuildIndexIfNeeded();

	// The attack index is empty unless a non-empty clash graph is assigned (BuildIndex gate), so this is a
	// zero-cost early-out for every project not using attack-vs-attack clash.
	if (IndexedAttackboxes.Num() == 0)
	{
		return false;
	}

	TArray<FWorldHitbox> AttackBoxes;
	if (!AttackerComponent->GetCachedWorldAttackBoxes(AttackBoxes))
	{
		return false;
	}

	const FClashGraph& ClashGraph = GetActiveClashGraph();
	AActor* AttackerOwner = AttackerComponent->GetOwner();
	TSet<int32> CandidateIndices;

	for (const FWorldHitbox& MyAttackBox : AttackBoxes)
	{
		CandidateIndices.Reset();
		GatherCandidates(MyAttackBox, AttackboxCells, CandidateIndices);

		for (int32 CandidateIndex : CandidateIndices)
		{
			if (!IndexedAttackboxes.IsValidIndex(CandidateIndex))
			{
				continue;
			}

			const FIndexedHurtbox& Candidate = IndexedAttackboxes[CandidateIndex];
			UPaper2DPlusCharacterProfileComponent* OtherComponent = Candidate.Component.Get();
			if (!IsValid(OtherComponent) || OtherComponent == AttackerComponent)
			{
				continue; // an actor's own attack boxes never clash with each other
			}

			AActor* OtherOwner = OtherComponent->GetOwner();
			if (!IsValid(OtherOwner) || OtherOwner == AttackerOwner)
			{
				continue;
			}

			const FWorldHitbox& OtherAttackBox = Candidate.Hurtbox; // FIndexedHurtbox.Hurtbox holds the box
			if (!IntersectHitboxes2D(MyAttackBox, OtherAttackBox))
			{
				continue;
			}

			// Resolve THIS attacker's perspective directly. ResolveClash is antisymmetric, so the OTHER
			// attacker's own query (OtherCat vs MyCat) yields the mirror outcome with no shared cache — the
			// two independent queries are automatically consistent (A AWins <=> B BWins).
			const EClashOutcome Outcome = Paper2DPlusClash::ResolveClash(MyAttackBox.ClashCategory, OtherAttackBox.ClashCategory, ClashGraph);

			FHitboxClashResult Result;
			Result.AttackBox = MyAttackBox;
			Result.OtherBox = OtherAttackBox;
			Result.OtherActor = OtherOwner;
			Result.Outcome = Outcome;
			Result.bAttackConnects = Paper2DPlusClash::ClashOutcomeDealsDamage(Outcome);

			const FBox2D MyBounds = GetHitboxBounds2D(MyAttackBox);
			const FBox2D OtherBounds = GetHitboxBounds2D(OtherAttackBox);
			const FBox2D Overlap(
				FVector2D(FMath::Max(MyBounds.Min.X, OtherBounds.Min.X), FMath::Max(MyBounds.Min.Y, OtherBounds.Min.Y)),
				FVector2D(FMath::Min(MyBounds.Max.X, OtherBounds.Max.X), FMath::Min(MyBounds.Max.Y, OtherBounds.Max.Y)));
			Result.ClashLocation = Overlap.GetCenter();

			OutResults.Add(Result);
		}
	}

	return OutResults.Num() > 0;
}

void UPaper2DPlusHitboxSubsystem::MarkIndexDirty()
{
	bIndexDirty = true;
}

void UPaper2DPlusHitboxSubsystem::BuildIndexIfNeeded()
{
	// Audit F3: rebuild on an explicit dirty, on a new engine frame (the per-frame cap), OR when any
	// indexed component MOVED since the index was built this frame (a same-frame movement makes the
	// snapshot + spatial cells stale, so an authority hit can't depend on query order).
	//
	// Cost note (re-review): the drift check is O(indexed components) per query and, when it trips, a full
	// rebuild. In the common UE pattern (all movement in an earlier tick phase, all hit queries after) the
	// transforms are stable across the frame's queries, so it stays one build per frame. The pathological
	// case is movement INTERLEAVED with queries within a frame across many actors → up to O(queries) full
	// rebuilds; incremental per-component re-cell is the optimization if profiling ever shows it (TASK-98).
	// Known gap (re-review): drift is transform-only, so a STATIONARY defender whose flipbook advances to a
	// new key-frame (same transform, different hurtbox shape) AFTER the build is still served stale for the
	// rest of the frame — at most one frame, matching pre-F3 behavior; the manual PIE matrix covers it.
	if (bIndexDirty || BuiltFrameCounter != GFrameCounter || HasIndexedComponentTransformDrift())
	{
		BuildIndex();
	}
}

FTransform UPaper2DPlusHitboxSubsystem::ResolveIndexTransform(const UPaper2DPlusCharacterProfileComponent* Component)
{
	if (Component)
	{
		if (const UPaperFlipbookComponent* FBComp = Component->GetResolvedFlipbookComponent())
		{
			return FBComp->GetComponentTransform();
		}
	}
	return FTransform::Identity;
}

bool UPaper2DPlusHitboxSubsystem::HasIndexedComponentTransformDrift() const
{
	for (const TPair<TWeakObjectPtr<UPaper2DPlusCharacterProfileComponent>, FTransform>& Pair : IndexedComponentTransforms)
	{
		const UPaper2DPlusCharacterProfileComponent* Component = Pair.Key.Get();
		if (!IsValid(Component))
		{
			return true; // an indexed component vanished — the snapshot referencing it is stale
		}
		if (!ResolveIndexTransform(Component).Equals(Pair.Value))
		{
			return true; // moved since the index built this frame
		}
	}
	return false;
}

void UPaper2DPlusHitboxSubsystem::BuildIndex()
{
	IndexedHurtboxes.Reset();
	HurtboxCells.Reset();
	IndexedAttackboxes.Reset();
	AttackboxCells.Reset();
	IndexedComponentTransforms.Reset();

	// TASK-77 U4: build the PARALLEL attack-box index only when the game actually USES attack-vs-attack clash
	// (bAttackClashUsed, set on the first QueryAttackClashes call) AND a non-empty clash graph is assigned. A
	// U3-only project (defensive gate) assigns a graph but never queries clashes, so it pays nothing here; the
	// shipped DefaultClashGraph=None pays nothing either. The hurtbox index below is byte-identical regardless.
	const bool bBuildAttackIndex = bAttackClashUsed && GetActiveClashGraph().Edges.Num() > 0;

	for (auto It = RegisteredComponents.CreateIterator(); It; ++It)
	{
		UPaper2DPlusCharacterProfileComponent* Component = It->Get();
		if (!IsValid(Component))
		{
			It.RemoveCurrent();
			continue;
		}

		bool bContributed = false;

		TArray<FWorldHitbox> Hurtboxes;
		if (Component->GetCachedWorldHurtboxes(Hurtboxes))
		{
			for (const FWorldHitbox& Hurtbox : Hurtboxes)
			{
				const int32 HurtboxIndex = IndexedHurtboxes.Add({Component, Hurtbox});
				AddBoxToCells(HurtboxIndex, Hurtbox, HurtboxCells);
			}
			bContributed = true;
		}

		// The attacker's attack boxes were already built by the same RefreshCachedWorldState pass that built
		// the hurtboxes (GetCachedWorldAttackBoxes is a cheap cached copy) — only the cell insertion is new.
		if (bBuildAttackIndex)
		{
			TArray<FWorldHitbox> AttackBoxes;
			if (Component->GetCachedWorldAttackBoxes(AttackBoxes))
			{
				for (const FWorldHitbox& AttackBox : AttackBoxes)
				{
					const int32 BoxIndex = IndexedAttackboxes.Add({Component, AttackBox});
					AddBoxToCells(BoxIndex, AttackBox, AttackboxCells);
				}
				bContributed = true;
			}
		}

		// Audit F3 (merged with TASK-77 U4): record the build-time transform of every CONTRIBUTING component
		// — hurtboxes OR attack boxes — so a later same-frame move of any of them is detected (a
		// non-contributing component can't make either index stale). An attack-only contributor must be
		// tracked too, or a same-frame mover would serve QueryAttackClashes a stale attack index.
		if (bContributed)
		{
			IndexedComponentTransforms.Add(Component, ResolveIndexTransform(Component));
		}
	}

	BuiltFrameCounter = GFrameCounter;
	bIndexDirty = false;
#if !UE_BUILD_SHIPPING
	++Test_IndexRebuildCount;
#endif
}

void UPaper2DPlusHitboxSubsystem::AddBoxToCells(int32 BoxIndex, const FWorldHitbox& Box, TMap<FIntPoint, TArray<int32>>& Cells)
{
	const FBox2D Bounds = GetHitboxBounds2D(Box);
	const FIntPoint MinCell = GetCellForPoint(Bounds.Min);
	const FIntPoint MaxCell = GetCellForPoint(Bounds.Max);

	for (int32 X = MinCell.X; X <= MaxCell.X; ++X)
	{
		for (int32 Y = MinCell.Y; Y <= MaxCell.Y; ++Y)
		{
			Cells.FindOrAdd(FIntPoint(X, Y)).Add(BoxIndex);
		}
	}
}

void UPaper2DPlusHitboxSubsystem::GatherCandidates(const FWorldHitbox& QueryBox, const TMap<FIntPoint, TArray<int32>>& Cells, TSet<int32>& OutCandidateIndices) const
{
	const FBox2D Bounds = GetHitboxBounds2D(QueryBox);
	const FIntPoint MinCell = GetCellForPoint(Bounds.Min);
	const FIntPoint MaxCell = GetCellForPoint(Bounds.Max);

	for (int32 X = MinCell.X; X <= MaxCell.X; ++X)
	{
		for (int32 Y = MinCell.Y; Y <= MaxCell.Y; ++Y)
		{
			if (const TArray<int32>* CellEntries = Cells.Find(FIntPoint(X, Y)))
			{
				for (int32 Index : *CellEntries)
				{
					OutCandidateIndices.Add(Index);
				}
			}
		}
	}
}

FIntPoint UPaper2DPlusHitboxSubsystem::GetCellForPoint(const FVector2D& Point) const
{
	const float SafeCellSize = FMath::Max(CellSize, 1.0f);
	return FIntPoint(
		FMath::FloorToInt(Point.X / SafeCellSize),
		FMath::FloorToInt(Point.Y / SafeCellSize));
}

FBox2D UPaper2DPlusHitboxSubsystem::GetHitboxBounds2D(const FWorldHitbox& Hitbox)
{
	const FVector2D Center(Hitbox.Center.X, Hitbox.Center.Z);
	const FVector2D Extents(FMath::Abs(Hitbox.Extents.X), FMath::Abs(Hitbox.Extents.Z));
	return FBox2D(Center - Extents, Center + Extents);
}

bool UPaper2DPlusHitboxSubsystem::IntersectHitboxes2D(const FWorldHitbox& A, const FWorldHitbox& B)
{
	return GetHitboxBounds2D(A).Intersect(GetHitboxBounds2D(B));
}
