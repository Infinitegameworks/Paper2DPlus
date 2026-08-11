# Paper2DPlus Multiplayer Authority Contract

> ## AUTHORITATIVE (current surface synchronized 2026-07-10)
>
> This contract classifies every networked Paper2DPlus runtime API and states the game-side
> cooperation it assumes. The enforcing layer shipped across PRs #193–#201 (units U1–U8 + a round-2 review-fix pass); the
> origin plan is `docs/plans/2026-06-11-001-feat-paper2dplus-network-replication-plan.md`.
> **Implementation patterns** (how the replication is built, the reusable net-driver gotchas) live
> separately in `docs/solutions/ue-paper2dplus-network-authority-patterns.md` — this document is the
> *what/where* (API classification + game contract); that one is the *how*. Replication is **strictly
> opt-in per component**; with it off, every API behaves exactly as pre-networking (the `Standalone`
> context, every gate passing). The **Tier-1 worldless suites** (`Paper2DPlus.Network.*` plus focused
> `Paper2DPlus.FrameCues.*` coverage) pin the gating/snapshot/clock logic; the **manual PIE
> matrix** below exercises the real net driver end-to-end. (An executable two-world `FTestWorlds`
> suite was prototyped but the headless automation harness can't cleanly host transient-asset net
> replication — the net driver floods `LogNetPackageMap` warnings for the non-net-stable test assets,
> which the automation framework treats as failures — so the real-driver scenarios ship as the
> documented manual PIE matrix, per the plan's best-effort downgrade.)

## Scope and core rules

Paper2DPlus replication is **strictly opt-in per component** (`bEnableReplication`, default
`false`, arrives in U2). With replication off — or in any single-player / worldless context — the
component resolves `EPaper2DPlusNetContext::Standalone` and **every authority gate passes
unconditionally**: single-player behavior is preserved by identity (constraint C1).

When replication is enabled, the contract below applies. The four contexts:

| Context | Meaning |
|---|---|
| `Standalone` | Not networked (replication off / no world / `NM_Standalone`). Every gate passes. |
| `Authority` | The server-side instance (listen or dedicated). The only context allowed to mutate gameplay state. |
| `AutonomousProxy` | The owning client's instance. Plays owner cosmetics and receives replicated state; game-owned RPCs route gameplay intent. |
| `SimulatedProxy` | Any other client's instance. Receives replicated state; world-visible cosmetics only. |

**Golden rule (the `NetPolicy` mandate, verbatim from the property tooltip):** *Any Cue that
mutates gameplay state MUST be AuthorityOnly; LocalAlways is for per-machine cosmetics only.* The
same principle generalizes to every API below: gameplay mutation belongs on the authority.

Paper2DPlus no longer owns transition input/confirmation, so prediction and move-start latency are
game-state-machine concerns. Replicated animation catch-up still suppresses Cue replay.

## Classification legend

| Classification | Meaning |
|---|---|
| **Server-authoritative** | Call on the authority only. Non-authority calls are gated (warn + no-op); replicated component state may flow down. |
| **Cosmetic per-machine** | Runs locally on each machine for presentation; never gameplay truth. |
| **Local-only** | Never replicates by design (previews, debug, editor-feeding state). |
| **Both-safe query** | Pure read; callable anywhere, but on non-authority machines it reads *local* (possibly stale/visual) state — never use it to adjudicate gameplay off-authority. |

---

## UPaper2DPlusCharacterProfileComponent

### Playback and transition data

The Character Profile transition map is pure From-to data. Paper2DPlus exposes the flipbook-keyed
Animation Map resolver (`GetAnimationTransitionInfo` / `ResolveAnimationTransition`, Profile + flipbook only), but it has **no transition request RPC, input
buffer, move-outcome driver, or automatic playback switch**. The game/PaperZD owns input routing and
state-machine legality on authority.

| API | Classification | Networked notes |
|---|---|---|
| `GetCurrentMoveName()` / `GetCurrentMoveFlipbook()` | Both-safe query | Resolve the local flipbook. A proxy reads visual state, not adjudication truth. |
| `SetCharacterProfile(NewProfile)` | Server-authoritative when replication is enabled | Non-authority networked calls warn and return. The OnRep funnel adopts the server profile. Standalone passes. |
| `HandleFlipbookChanged` / `HandleFrameChanged` | Local engine-driven | Run on each machine from local playback. Authority publishes animation state; proxy catch-up suppresses one-shot Cue replay. |
| `OnReplicatedAnimStateChanged` / `RebroadcastReplicatedAnimState()` | Cosmetic/advisory receive seam | The game may perform proxy playback in advise-only mode. The optional component apply mode is mutually exclusive with a second game-side playback driver. |
| `RepublishAnimState()` | Server-authoritative | Re-anchors after a game-side mid-move playback mutation that the component cannot observe immediately. |

### Hit validation and dedup

| API | Classification | Networked notes |
|---|---|---|
| `ValidateAndRegisterHit(...)` | Server-authoritative | Re-queries the server hitbox state, verifies the supplied victim is in the result, samples the latest attack key frame in the crossed span, and deduplicates once per move-instance/victim/hit-window. It never applies damage. |
| `RegisterHitOnce(...)` | Server-authoritative | Dedup-only path for a game that already performed its own authoritative hit adjudication. It does not re-query overlaps. |
| `GetCurrentHitWindowIndex()` | Both-safe local query | Advisory on a proxy. Never echo a client-observed explicit window index into either authoritative registration call; prefer `-1` so authority derives it. |
| `ResetHitDedupWindow()` | Server-authoritative mutation | Explicitly reopens the current move's dedup window for game-driven multi-hit cadence. |

### Automatic hit detection (TASK-145)

| API | Classification | Networked notes |
|---|---|---|
| `bAutoHitDetection` | Server-authoritative feature | Arming, detection passes, and every TASK-145 broadcast run **only** where adjudication runs (`Paper2DPlusNetGating::ShouldRunAutoHitDetection` — Standalone/Authority). A proxy with the flag set never arms and never broadcasts; proxy cosmetics ride Frame Cues (author a `CosmeticOnly` Cue State over the active frames) or the game's own replication of the authoritative outcome. |
| `OnHitConnected` / `OnHitReceived` | Authority-side broadcasts | Fire on the adjudicating machine only — attacker component and victim component respectively, same payload. Hits register through the SAME `ProcessedHits` ledger as `ValidateAndRegisterHit` (once per victim/move-instance/"HitWindow" window), so mixing auto and manual registration cannot double-hit. The plugin never applies damage; the receiver does, and replicates its own result. |
| `OnAttackWindowBegin` / `OnAttackWindowEnd` / `OnAttackWhiffed` | Authority-side broadcasts | Derived from the authored attack frames on the adjudicating machine. Whiff = the move instance armed at least one window but registered zero hits (auto **or** manual); suppressed on `EndPlay` teardown. |
| `TickAutoHitDetection()` | Server-authoritative (subsystem-driven) | The hitbox subsystem's armed-set tick calls it between key-frame transitions so movement during a held active frame still connects. Dedup-safe by construction; no-op while a frame dispatch is live on the same stack. |

**Detection sampling caveat:** the auto pass queries the attacker's **current-frame** cached attack
boxes. A 1-frame active window crossed entirely inside one coarse tick (the TASK-91 lag-comp case)
under-detects — fail-safe (no hit), never a double-hit; the span-parameterized box build tracked by
TASK-91 closes it. With the event-driven `UPaper2DPlusFlipbookComponent` every rendered key frame
gets its own pass, so this affects only frame-skipping ticks.

### Hit-stop

| API | Classification | Networked notes |
|---|---|---|
| `TriggerHitStop(Other, Duration)` | Server-authoritative (replicated cosmetic effect) | Trigger explicitly on authority after the game's confirmed impact; freezes replicate as a self-expiring `RepHitStop` snapshot and each machine applies its own `CustomTimeDilation` freeze. Local client calls are local-only and must not represent gameplay truth. |
| `CancelHitStop()` | Server-authoritative | Publishes `Duration=0`; `EndPlay` does the same so travel/death never strands a live freeze. |
| `IsHitStopActive()` | Both-safe query | Per-machine freeze state. Covers the U8 drift-snap suppression on a proxy under a replicated freeze. |

**Hit-stop clock-model caveat (U5/KTD-19):** the freeze pauses the flipbook (the move clock) via whole-actor `CustomTimeDilation`, so the authority advances `RepAnimState.StartServerTime` by the *real* frozen span on unfreeze (same `Sequence` — clients re-derive). This anchor stays truthful **only** while the freeze is the sole thing pausing that actor's clock. If the game layers its OWN per-actor `CustomTimeDilation` slow-mo on top, it pauses/slows the same move clock the same way and the anchor will drift — call `RepublishAnimState()` after such a mutation (the documented escape hatch). Whole-WORLD time dilation is safe: `GetServerWorldTimeSeconds()` dilates with it, so the server-time anchor and the flipbook clock stay aligned. The replicated freeze is **cosmetic state** — set `bReplicateHitStop=false` to suppress it where host-authority freeze cosmetics are unwanted. The refcounted freeze registry is **per machine** (refcounts legitimately differ across machines — each applies the overlapping snapshots it received); a victim that carries its own profile component with `bReplicateHitStop` gets a `Victim=self` snapshot published on ITS component (KTD-24), removing the cross-actor pointer relevancy dependency. Null victim = attacker-only freeze (tolerated, no warn).

**Receiver new-vs-extend (U5):** a proxy distinguishes a genuinely NEW freeze from an EXTEND by the published `HitStopSeq` (the authority bumps it only when the prior freeze had fully ended; an extend keeps it). If a distinct-seq snapshot lands while a stale local freeze is still draining (the prior freeze's clear coalesced away on the wire), the proxy REPLACES the remaining with the new clamped span rather than `Max`-extending it, so it adopts the server's *current* freeze. A same-seq re-snapshot still never shortens (jitter/duplicate tolerance).

**Known networked edges (accepted — cosmetic, self-correcting):**
- *Listen-server / dedicated-server double-broadcast (by design):* with KTD-24, a victim carrying its own replicating component refcounts the victim actor twice on the authority (attacker freeze + victim-component freeze) and the victim component broadcasts its own `OnHitStopBegin`/`End`. This mirrors what a remote client sees (the victim freeze arrives on the victim's own component there too) and is refcount-balanced — the actor restores only when *both* ends complete. The freeze is applied on a dedicated server (no viewer) because it is a real gameplay pause (both fighters pause) and the authority needs the frozen span to drive the clock re-anchor; it is not view-only.
- *Seamless-travel re-anchor (narrow):* a travel-*persistent* actor that keeps a live freeze across a level-load hitch can have `ActualFrozenSeconds` accumulate the hitch, transiently mis-anchoring *that move's* proxy playback until the next move re-stamps the anchor. Non-persistent actors publish a `Duration=0` clear on `EndPlay` before travel, so this is bounded to persistent-actor-mid-freeze and is cosmetic (anim position only; combat truth is unaffected).
- *Stale victim pointer:* `RepHitStop.Victim` resolves through the net-GUID cache; an unresolved/relevancy-dropped pointer yields an attacker-only freeze (`FreezeActorOnce` is `IsValid`-guarded). A theoretical GUID-reuse-to-a-different-actor briefly freezes a bystander, self-expiring via the registry — KTD-24 removes the dependency entirely for victims with their own component.
- *Registry refcount on GC-without-EndPlay:* a component GC'd without `EndPlay` strands its registry refcounts until the actors die (a pre-existing PR4 condition; in-world components always `EndPlay`). The self-expiring real-time ticker is the stranding defense for the common paths.
- *Re-entrant trigger inside a Begin/End handler:* a follow-up `TriggerHitStop`/freeze issued from inside an `OnHitStopBegin`/`End` handler is rejected (the `bBroadcastingHitStop` re-entry guard) and logged — consistent with the PR4 guard discipline.

### Root motion

| API | Classification | Networked notes |
|---|---|---|
| `bAutoApplyRootMotion` / `SetAutoApplyRootMotion` | Server-authoritative apply | World offsets apply on Standalone/Authority ONLY (U8); proxies advance baselines without moving actors — movement replication owns proxy motion. CMC pawns: set `bAutoApplyRootMotion=false` and integrate game-side; `BeginPlay` warns on the conflicting combination. |
| `GetRootMotionDelta()` | Both-safe query | Stays a meaningful per-frame advisory on proxies (baselines advance without applying). |
| `ConsumeRootMotionDelta()` | Server-authoritative | On non-authority networked contexts returns **zero** and advances the baseline (warn-once, U8). Manual-drive consumers must run on authority. |
| `ResetRootMotionTracking()` | Server-authoritative | Mutates the baseline the authority's applies derive from; call where the motion is applied. |

### Profile & playback plumbing

| API | Classification | Networked notes |
|---|---|---|
| `HandleFlipbookChanged` / `HandleFrameChanged` | Local-only (engine-driven) | Delegate endpoints driven by local playback on every machine. On authority, `OnFlipbookChanged`'s tail publishes the anim-state snapshot (U2). Non-funnel flipbook changes on non-authority networked contexts are treated catch-up-grade (one-shots suppressed, KTD-23). |
| `GetResolvedFlipbookComponent()` | Both-safe query | — |
| `GetCurrentRuntimeFrameState(...)` | Both-safe query | Local animation state — visual on proxies, truth on authority. |
| `GetNetContext()` | Both-safe query | The resolved gating context itself (U1: `Standalone` unconditionally; real role mapping U2). |
| `RepublishAnimState()` *(new, U8)* | Server-authoritative | Re-anchors the playback clock after game-side mid-move playback mutations (slow-mo supers, manual `SetPlaybackPosition`). |

### Delegates (per-machine broadcasts — NOT replicated events)

Every `BlueprintAssignable` on the component broadcasts **on the machine where its trigger ran**,
never across the network:

| Delegate | Fires on (networked) | Notes |
|---|---|---|
| `OnFrameCue` | Whichever machines the Cue's `NetPolicy` admits | The gating matrix below admits the Cue Type's own behavior first, then this listener broadcast on the same machine. |
| `OnHitStopBegin` / `OnHitStopEnd` | Every machine applying the replicated freeze (U5) | Per-machine cosmetic bookends. |
| `OnHitConnected` / `OnHitReceived` / `OnAttackWindowBegin` / `OnAttackWindowEnd` / `OnAttackWhiffed` | Adjudicating machine only (Standalone/Authority) | TASK-145 auto-detection broadcasts — see the Automatic hit detection table above. |
| `OnReplicatedAnimStateChanged` | Receiving clients (and listen-server locals) | Advisory animation-state consumption point. The retained Confirmed Label output is always None because the transition driver was removed. |

---

## Frame Cues — `NetPolicy` gating matrix

Per-Cue `EPaper2DPlusFrameCueNetPolicy`, consulted by
`Paper2DPlusNetGating::ShouldDispatchFrameCue` (editor preview is editor-only and does not use a
gameplay world):

| Policy \ Context | Standalone | Authority (listen) | Authority (dedicated) | AutonomousProxy | SimulatedProxy |
|---|---|---|---|---|---|
| `LocalAlways` (default) | ✓ | ✓ | ✓ | ✓ | ✓ |
| `AuthorityOnly` | ✓ | ✓ | ✓ | ✗ | ✗ |
| `CosmeticOnly` | ✓ | ✓ | ✗ (no rendering) | ✓ | ✓ |
| `OwnerOnly` | ✓ | ✓ if locally controlled | ✗ | ✓ | ✗ |

Catch-up applies (late join, profile-OnRep re-warm, non-funnel changes off-authority): Cues
never dispatch; Cue State Begins fire only for cosmetic-grade policies, with `bIsCatchUp=true` in the
context.

Defaults: the abstract `UPaper2DPlusCueBase` base declares **LocalAlways**, but a **newly created Cue
Type is stamped CosmeticOnly** by both creation flows, so that is what a designer actually starts from.
Reclassify any gameplay-mutating Cue to `AuthorityOnly` yourself. (v8.0.0 deleted the six built-in Cue
classes, so there are no shipped per-class defaults to quote here any more — every concrete Cue is a Cue
Type the project authors and owns.)

The policy gates the **broadcast** and the Cue's own behavior together. Since v8.0.0 a Cue Type may
implement behavior that runs immediately before the broadcast; the policy decides whether either
happens on a given machine. A receiver — or a Cue behavior — that spawns a replicated
actor, applies tags, or mutates combat state must run on authority using the game's normal networking.
Cues never replay during catch-up. Cosmetic-grade Cue States can rebuild their Begin with
`bIsCatchUp=true`; an AuthorityOnly Cue State Begin does not replay.

---

## UPaper2DPlusBlueprintLibrary

| API group | Classification | Networked notes |
|---|---|---|
| Conversion helpers (`HitboxToWorldSpace*`, `SocketToWorldSpace*`) | Both-safe query | Pure math. |
| Collision/hit queries (`CheckAttackCollision`, `QuickHitCheck`, `QueryActorAttackOverlaps`, `GetHitboxFrame`) | Server-authoritative for gameplay; Cosmetic per-machine otherwise | They read each machine's **local** animation frame (R3): on clients that is whatever frame is rendering — usable for VFX/feel only. Damage adjudication must use the server's query (and, from U4, `ValidateAndRegisterHit`). |
| World/local hitbox + socket getters (`GetActorWorld*`, `GetActorLocal*`) | Both-safe query | Local-frame reads; cosmetics off-authority. |
| Frame data reads (`GetFrameDamage`, `GetFrameKnockback`, `FrameHasAttack`, `IsFrameInvulnerable`) | Both-safe query | Asset data at the *local* frame — authoritative numbers come from the server's hit validation result. |
| `SetActorCharacterProfile` | Server-authoritative | Thin wrapper over `SetCharacterProfile` — same gate (U2). |
| Curve/Cue queries | Both-safe query | Asset-level reads are identical everywhere; actor-level curve reads resolve local playback. Cue queries never execute behavior. |
| Root motion (`GetRootMotionAtFrame`, `GetActorRootMotionDelta`) | Both-safe query | Advisory on proxies (U8 keeps baselines advancing). |
| `ConsumeActorRootMotionDelta` | Server-authoritative | Mutating — same rule as the component's `ConsumeRootMotionDelta`. |
| `GetTotalDamage` / `GetMaxKnockback` | Both-safe query | Pure aggregation. |
| `DrawActorDebugHitboxes` | Local-only | DevelopmentOnly debug draw of the local machine's state. |

---

## UPaper2DPlusAnimationMapLibrary

| Surface | Classification | Networked notes |
|---|---|---|
| Chain-opener discovery (`GetComboOpenerFlipbooks`) and direct transition/phase resolution | Both-safe query | Reads authored Character Profile data identically everywhere. It returns inspection or one deterministic flipbook result only; it never requests, confirms, or replicates a playback switch. |

---

## UPaper2DPlusCombatProfileAsset / Component

Combat Profiles do not replicate decisions and never play moves. Their asset scoring is deterministic
for the same data/context; weighted picking additionally advances the caller/component random stream.

| Surface | Classification | Networked notes |
|---|---|---|
| `BuildAttackCatalog` / `ScoreAttackOptions` | Both-safe advisory query | Static asset/context evaluation. A proxy can use it for UI, but authoritative AI must build context from server-owned state. |
| `GetCombatDecision` | Both-safe advisory query | Returns the best ranked advice; the game still checks state-machine legality/resources and plays the move. |
| `PickWeightedCombatAttack` | Server-authoritative when it drives gameplay | Advances a local random stream. Run on authority and replicate the resulting game state, not the transient decision object. |
| `MakeTargetContext` / `GetCombatDecisionForTarget` | Server-authoritative when used for gameplay | Actor distance is read from that machine's local transforms. Proxy results are presentation-grade only. |
| `OnCombatDecisionUpdated` | Per-machine broadcast | Fires where a component decision call ran; it is not a replicated event. |
| `CooldownSeconds`, `bUseWeightedSelection`, `TransitionOutcomeRules`, `CancelCategories` | Data/advisory or dormant | Cooldown is not enforced; the selection flag and transition foundation are not current runtime drivers. The game owns these policies. |

---

## UPaper2DPlusEffectProfileAsset / effect-carrying Cues

| Surface | Classification | Networked notes |
|---|---|---|
| Effect membership and Type/Descriptor queries | Both-safe query | Static visual-library data. They spawn nothing and carry no replicated state. |
| Character-to-Effect assignment | Editor/Catalog authority | The Character Catalog owns the relationship. It is authoring metadata, not a network lookup during Cue dispatch. |
| A Cue Type's ordinary art/placement variables | Both-safe data query | Reads the placement's direct values. Scalar soft Paper Flipbook variables are structurally warmed before traversal; Cue dispatch never loads an Effect Profile or Catalog at runtime. |
| Effect-spawning Cue behavior or receiver | Cosmetic per-machine by default | A new Cue Type is stamped CosmeticOnly. The game owns pooling/spawn/lifetime and must not turn a cosmetic path into gameplay truth. |
| ~~`UPaper2DPlusSpawnEffectCue::ResolveSpawnSettings`~~ | — | **REMOVED in v8.0.0 (TASK-165)** with the six built-in Cue classes; no migration, no CoreRedirect. |

---

## UPaper2DPlusCharacterCatalogAsset

| Surface | Classification | Networked notes |
|---|---|---|
| `GetDefaultCharacterCatalog` | Both-safe soft query | Returns a typed soft reference without loading it. Every machine needs the same cooked Catalog/asset content. |
| Catalog entry/tag/group/completion queries | Both-safe query | Read the saved cook-safe snapshot and return soft references. They never spawn actors, choose encounters, or synchronously load companions. |
| Catalog Sync / relationship edits | Editor-only authority | Explicit editor workflow; no runtime mutator or replication protocol exists. |
| `Paper2DPlusValidate` configured-Catalog requirement | Read-only project authority gate | Unfiltered or Catalog-scoped validation emits a stable Error when `DefaultCharacterCatalog` is unset, missing, or resolves to the wrong class. A valid Catalog/subclass remains owned by the normal shared adapter and is not duplicated. Path filters apply honestly; an unset reference has no invented path. |
| Game selection derived from Catalog | Server-authoritative game responsibility | Choose/spawn on authority and replicate the game's selected actor/state through game systems. Do not replicate a live mutable Catalog. |

---

## UPaper2DPlusHitboxSubsystem

| API | Classification | Networked notes |
|---|---|---|
| `RegisterProfileComponent` / `UnregisterProfileComponent` | Local-only (per-world bookkeeping) | A `UWorldSubsystem` — each world (server, every client) keeps its own broadphase. Registration happens wherever the component lives. |
| `QueryAttackOverlaps` | Server-authoritative for gameplay; Cosmetic per-machine otherwise | The broadphase indexes **local** hurtbox state. The server's instance is the one U4's hit validation re-queries; client instances are cosmetic-grade. **TASK-77 U3** added a defender-DefenseClass clash gate INSIDE this query (suppress an overlap when the defense beats the attacker's category, per `DefaultClashGraph`); the verdict is pure + deterministic (`Paper2DPlusClash::AttackConnects`) so the server re-derives it identically — i-frame/untagged behaviour is a provable no-op. |
| `QueryAttackClashes` *(TASK-77 U4)* | **Both-safe query** (advisory; gate+advise) | ADVISORY attack-vs-attack clash query over a SEPARATE parallel attack-box index — REPORTS a per-clash `FHitboxClashResult.Outcome` (Trade/Clash/AWins/BWins); it does NOT apply or suppress damage and does NOT touch `QueryAttackOverlaps` / `ValidateAndRegisterHit` (the authoritative hurtbox path is byte-identical). The game reconciles clash outcomes the way it already reconciles hits. Each pair's *verdict* is order-invariant by `ResolveClash` antisymmetry, so the two attackers' independent queries agree on every machine with no shared state — **but the result-ARRAY order is unspecified** (hash/insertion order), so consumers must process all results (or sort by a stable key), never index positionally. Off-authority it reads local frame state (advisory only). No results unless the game has called `QueryAttackClashes` AND a non-empty clash graph is assigned (otherwise zero-cost — no attack index is built). |
| `MarkIndexDirty` | Local-only | Per-world index invalidation. |

---

## UPaper2DPlusLayerRenderComponent (appearance authority and presentation)

The game forwards owner intent through its own RPC/input channel and calls committed appearance setters on
authority. Paper2DPlus has no client→server appearance RPC. With component replication enabled, the server
publishes one canonical descriptor; each client derives gameplay data and chooses its own visual tier.

| API/state | Classification | Networked notes |
|---|---|---|
| `ApplyAppearancePreset` / `SetLayerActive` / `ResetToDefaultAppearance` | Server-authoritative committed mutation | Selection normalizes and enforces optional Exclusive Groups; gameplay hitboxes and Frame Cues recompose first, then `OnLayersChanged` advises visuals, then the descriptor publishes. No-op or invalid mutations publish nothing. |
| `GetAppearanceDescriptor` | Both-safe query | Serializable intent only: payload version, delivery identity, and normalized active Layer IDs. It contains no visibility, pixels, primitive, cache, tier, recolor, actor, or component state. |
| `ApplyAppearanceDescriptor` | Server-authoritative restore | Accepts compatible normalized intent. Fixed / Baked rejects runtime selection mutation; Runtime Customizable validates stable Layer IDs and exclusivity. A restored authority value republishes normally. |
| Default Appearance | Server-authoritative config | The required complete default seeds committed state even on a dedicated server; the authority's normalized descriptor wins. |
| `SetLayerVisible` / `RefreshLayers` | Local compatibility presentation | Never changes the descriptor or replication. Current Runtime Customizable games should use committed APIs rather than manual visibility. |
| `SetLayerRecolor` / `ClearLayerRecolor` | Local presentation | Re-applies the live material or requests a new local hybrid cache key; does not recompose gameplay or publish a descriptor. |
| `bEnableHybridRuntimeRenderer` | Client-local opt-in | Runtime Customizable defaults to exact all-live rendering. Hybrid changes representation only and never runs concurrently with steady all-live output. |
| `SetAppearancePriorityOverride` / tier and pending-reason queries | Client-local scheduling/diagnostics | Priority, near/far/changing/off-screen tier, queue, cache, and pressure state never replicate. |
| `OnLayersChanged` | Per-machine advisory | Fires after committed logical/gameplay state is current; clients also receive it after a descriptor OnRep apply. |

**Wire and lifecycle invariants:**

- `FPaper2DPlusRepAppearanceState` wraps the normalized `FPaper2DPlusAppearanceDescriptor` with payload version and monotonic sequence. Canonical Layer ordering makes equivalent selections byte-equivalent; latest sequence wins and repeated same-sequence delivery is idempotent.
- The descriptor is the only appearance selection model on the wire. Derived visibility, composed boxes/Cues, render targets, cache keys, tier, priority, recolor, and preview state are forbidden payload fields.
- Appearance profile schema 3 measures the exact reflected `RepAppearance` **property value payload** with `FRepLayout::SerializePropertiesForStruct` and `FNetBitWriter`, round-trips it, and recursively checks that tier/cache/pixel/texture/object state is absent. Its bit/ceiling-byte fields exclude RepLayout handles/change masks, channel/bunch/packet framing, packet handlers, reliability, relevancy, retransmission, and connection fan-out; they are not packet or on-wire byte counts. The separate authority probe keeps 200 publishes constant across simulated 0/1/4/32-client fan-out, checks late join, and checks zero dedicated-server visuals.
- `bEnableReplication=false` remains opt-in/single-player identity. `ResolveNetContext()` is the one component-local authority gate.
- Dedicated servers seed defaults, apply descriptors, and compose gameplay, but own zero sprite/composite visual children. The local appearance budget subsystem and transient cache are presentation only.
- Delivery modes are exclusive: Fixed / Baked uses editor-published flipbooks and refuses runtime Layer composition; Runtime Customizable composes gameplay and selects exact all-live or hybrid visuals.
- OnRep applies without waiting on `BaseProfile`; deferring a late-join descriptor on an unrelated soft load could strand it. Animation-aware Layers project again when the real flipbook boundary arrives.

### Per-option gameplay composition

Selected Layers may carry per-animation boxes and stateless behavior-carrying Frame Cues. This is
**committed-only derived state** with no separate wire format:

| Surface | Classification | Networked notes |
|---|---|---|
| `Paper2DPlusLayerCombat::ComposeCombatFrames` / `ComposeFrameCues` | Both-safe pure derivation | Runtime Customizable reads `CookedGameplayAnimations`, resolves canonical flipbook identity, translates Layer placement, and walks selected eligible Layers in global asset order. Fixed / Baked is already compiled and fails closed. |
| `UPaper2DPlusCharacterProfileComponent::NotifyAppearanceCombatDirty` | Committed-tail push (both ends) | Receives the committed descriptor before visual advisory/replication. Preview and recolor paths never push it. |
| Cached collision readers and Frame Cue dispatcher | Existing authority policy | Consume the composed view transparently; server hit validation and each Cue's network policy remain unchanged. |
| Static base-profile queries | Base-only by contract | Use the explicit compose seam when equipment-aware data is required; visual cache/tier state is never a gameplay input. |

---

## Debug CVars

| CVar | Classification | Networked notes |
|---|---|---|
| `Paper2DPlus.ShowHitboxes` | Local-only | Draws the executing machine's local frame state — on a client that is rendered (visual) truth, not server truth. Compiled out of Shipping. |
| `Paper2DPlus.ShowFrameData` | Local-only | Same; per-machine overlay. |

---

## Game-cooperation matrix (U7)

Paper2DPlus is **gate+advise**: it can validate/deduplicate an authoritative hit and apply its explicit
hit-stop exception, but it never plays a game-selected move, applies damage, or moves a CMC pawn. It
replicates selected component state and gates Cue broadcasts; the game does the gameplay. Per
responsibility:

| Responsibility | Where it runs | Paper2DPlus role | Game role |
|---|---|---|---|
| **Playing flipbooks / flipping facing** | Server **and** every client | The flipbook is the move CLOCK — Paper2DPlus reads it (`GetPlaybackPosition`) and publishes `RepAnimState` from authority; on proxies it *advises* via `OnReplicatedAnimStateChanged` and (opt-in apply mode, U8) drives `SetFlipbook`/`SetPlaybackPosition` on a simulated proxy. **Never tick-disable the flipbook on the server** — it must advance so the server stays the clock. | The game/PaperZD performs the actual `SetFlipbook` on press (gate+advise) and mirrors facing; under apply mode it lets the component drive proxy playback. |
| **Move transitions / input** | Game-defined owning-client-to-server path | Character Profile supplies pure From-to/tag/combo data only. There is no Paper2DPlus transition RPC or input buffer. | Route input, validate state-machine legality on authority, and perform the switch through the game/PaperZD. |
| **Damage / hit validation** | Server | `ValidateAndRegisterHit` re-queries the hitbox subsystem on authority, deduplicates once per move-instance, and returns server-authored hit data. It does **not** apply damage or record a move outcome. | Apply damage game-side on the server. Use `RegisterHitOnce` only when the game already owns the authoritative overlap check. |
| **Hit-stop** | Server publishes; every machine applies | Explicit `TriggerHitStop` on authority publishes a self-expiring `RepHitStop`; each machine applies its own `CustomTimeDilation` freeze and the move clock re-anchors on unfreeze. | Decide which confirmed impacts deserve hit-stop and call it explicitly; do not expect a HitStop curve or hit notification to trigger it automatically. |
| **Root motion** | Authority applies world offsets | `bAutoApplyRootMotion` applies on Standalone/Authority only; proxies advance the baseline without moving (movement replication owns proxy motion). | **CMC pawns:** set `bAutoApplyRootMotion=false` and integrate root motion into the movement component game-side; `BeginPlay` warns on the conflicting combination. Locomotion replicates via the game's movement component, not Paper2DPlus. |
| **Runtime appearance** | Server intent → clients; local representation | The normalized committed descriptor replicates; recolor, priority, tier, cache, and pixels stay local. No client→server appearance RPC. Gameplay composition precedes visual advisory/publication. | Forward owner Layer/preset intent over the game's channel and call committed setters **on authority**. Let each client choose all-live/hybrid presentation. |
| **Frame Cue behavior** | Per the Cue `NetPolicy` | `ShouldDispatchFrameCue` gates the Cue's own behavior AND the data broadcast, in that order, and catch-up suppresses Cue replay. | Author policy honestly, then implement the side effect in the Cue Type's behavior or in a receiver. Gameplay mutation MUST be AuthorityOnly; world-visible VFX/SFX use CosmeticOnly; owner-only UI/camera uses OwnerOnly. |
| **Combat advice** | Server for gameplay; anywhere for preview/UI | Combat Profile derives, scores, and explains ranked options. It neither replicates the decision nor plays a move. | Build authoritative context on server, apply project-specific cooldown/resource/state gates, play the chosen move, and replicate resulting game state. |
| **Effect library** | Editor authoring; static data query anywhere | Effect Profile supplies flipbook membership and Type/Descriptor tags. A Cue Type stores its chosen flipbook and placement settings directly. | Spawn/pool cosmetic effects from the Cue's behavior or its receiver; never use library membership as gameplay truth or runtime name resolution. |
| **Character Catalog** | Static query anywhere; game selection on server | Returns cook-safe ordered entries and soft companion references without loading/spawning. | Async-load at game-defined boundaries, choose/spawn on authority, and replicate the chosen gameplay result. |

### Networking expectations

- Paper2DPlus does not define input prediction, transition confirmation, or rollback. The game's input/state-machine layer owns that latency policy.
- The game may play a cosmetic local flipbook before authoritative state arrives. Non-funnel proxy changes are catch-up-grade, so Cues do not double-fire when authority catches up; the game owns rejection correction.
- `NetUpdateFrequency >= 30 Hz` is recommended for responsive replicated animation state. A `UPaper2DPlusFlipbookComponent` provides event-driven key-frame changes; the stock component uses the slower polling fallback.
- **`bEnableReplication` must match** between the server and client archetypes (it is consumed at BeginPlay; a runtime flip is inert). Preload referenced profile assets so a proxy's `CharacterProfile` OnRep resolves without an unmapped-GUID stash.
- Starting/switching the flipbook creates the new move-instance dedup boundary; call `ResetHitDedupWindow` only for an intentional extra hit window.
- Static Combat, Effect, and Catalog assets are not replicated objects. Ensure identical cooked content on all machines and replicate game decisions/selected state, not mutable asset copies.

### Manual PIE test matrix

Run these by hand (the Tier-1 suite + Tier-2 two-world suite cover the rest); each is a scenario the worldless rigs can't reach because they never `BeginPlay` / never run a real net driver:

1. **Listen server, 2 players (`Play -> Number of Players: 2`, Net Mode: Play As Listen Server):** P1 attacks; both windows converge on the move/frame. An AuthorityOnly projectile Cue receiver spawns exactly one replicated actor, while CosmeticOnly sound/effect receivers run on visual machines. A host appearance swap converges to the same descriptor on the client.
2. **Dedicated server, 2 clients (Net Mode: Play As Client, 2 players):** gameplay-mutating Cue receivers run only on server; cosmetic receivers skip the dedicated server; the server creates zero appearance visuals while still composing and publishing committed state.
3. **Server AI Combat Profile decision:** construct context and decide on server, then verify the game plays/replicates one resulting move. Client-side Score UI may match but never drives authority.
4. **`p.net PktLoss=15`:** across a move change the simulated proxy converges with no duplicate Cue fires; hit-stop never strands a frozen client because snapshots self-expire.
5. **Late join:** connect mid-move; the client reconstructs move/position, Cue fire count is zero, active cosmetic Cue States rebuild with catch-up context, and a bind-then-pull `RebroadcastReplicatedAnimState` reaches a Blueprint that bound in BeginPlay.
6. **Seamless travel:** a travel-persistent actor mid-hit-stop does not strand (EndPlay publishes the clear for non-persistent actors); after travel the next move re-stamps the anim anchor (the narrow re-anchor caveat is documented under Hit-stop).
