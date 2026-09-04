# Paper2DPlus + PaperZD — Complementary, Not Competing

**TL;DR:** PaperZD owns animation selection and state-machine playback. Paper2DPlus owns the
character data attached to the flipbooks that are playing: hitboxes, sockets, Frame Cues, root
motion, curves, imports, and layered-character authoring. Use the same flipbooks in both systems;
do not make either system duplicate the other's job.

> **Current contract:** A Frame Cue placement carries reflected payload plus its Cue Type's
> overridable behavior. Behavior runs immediately before
> `UPaper2DPlusCharacterProfileComponent::OnFrameCue` broadcasts the same lifecycle context. The Frame
> Cues editor runs that behavior against its own isolated preview actor/world; it never executes the
> open level or the project's listener graph. The Context directly carries the exact Character Profile,
> live `UPaperFlipbookComponent`, and an explicit runtime/editor evaluation mode. It does not carry or
> require a PaperZD AnimInstance.

## Where each tool sits

| Layer | Concern | Owner |
|---|---|---|
| Art authoring | Pixels, source layers, tags, slices | Aseprite / CharacterForge |
| Import and reimport | Source art to sprites, flipbooks, frame durations, and layered-character data | Paper2DPlus |
| Animation state | Which animation plays, state transitions, blends, and graph-scoped notifies | PaperZD / game AnimBP |
| Directional art data | Optional 3–16-slot flipbook sets nested under one logical animation | Paper2DPlus Character Profile |
| Directional playback bridge | Mapping a resolved flipbook to a sequence and asking the state machine to play it | Project Blueprint / C++ |
| Per-frame character data | Hitboxes, sockets, Frame Cues, root motion, curves, and timing | Paper2DPlus Character Profile |
| Character appearance | Parts, skins, wearables, cosmetic effects, and saved outfits | Paper2DPlus Character Layer |
| Sprite rendering | `UPaperSprite` and `UPaperFlipbook` rendering | Epic Paper2D |

PaperZD answers “which animation should play now?” Paper2DPlus answers “given the flipbook and key
frame that are playing, what authored character data is active?”

## Precise division of labor

PaperZD or the game owns:

- the AnimBP/state machine and every decision to enter, leave, restart, or blend an animation;
- state variables, transition conditions, and state-scoped AnimNotifies;
- applying a replicated animation advisory when the project chooses the default advise-only path;
- consuming a resolved directional flipbook and mapping it to any separately authored PaperZD
  sequence; Paper2DPlus never switches the AnimBP or sequence player.

Paper2DPlus owns:

- `UPaper2DPlusCharacterProfileAsset`, whose animation entries reference the shared flipbooks and
  optional PaperZD sequences;
- optional partial multidirectional flipbook sets beneath those entries, PaperZD-compatible vector
  selection, validation, and three cooked Blueprint queries;
- per-key-frame combat hitboxes, hurtboxes, sockets, root motion, auxiliary curves, and Frame Cues;
- pure From-to transition arrows plus flipbook/tag-keyed, chain-start-bounded Animation Map inspection and
  resolution used as authored data—the plugin does not request or confirm playback transitions;
- import/reimport, extraction, validation, and Character Layer authoring.

The PaperZD bridge is optional at the plugin boundary. The `.uplugin` marks PaperZD optional, and
the runtime build detects an available installation and sets `WITH_PAPERZD`. Public asset fields and
Blueprint accessors still use `UObject` references, while sequence discovery reads PaperZD asset data
through reflection. A project without PaperZD can use the rest of Paper2DPlus.

Directional Profile data does not change that boundary. `Resolve Directional Flipbook` returns a
`UPaperFlipbook`, not a `UPaperZDAnimSequence`. It uses PaperZD 2.2.4's +Y-zero,
clockwise-positive, half-sector-rounded angle convention so a project can share facing math, but it
does not create, link, or play directional sequences. The Profile's base animation remains the
identity and gameplay-data owner; its variants are art aliases. The existing replicated animation
advisory remains base-only because its payload contains no facing direction.

The Bulk Extractor's **PaperZD Sequences** action is stricter still: the editor module takes no
PaperZD or PaperZDEditor dependency. The button appears only after the optional plugin is installed
and enabled, both fully qualified PaperZD classes resolve through reflection, and their required
object/array property types are compatible. Missing or incompatible classes or schema fail closed,
leaving the rest of extraction available.

### Proving the optional-absent package

Installed developer machines can accidentally leave the absent-dependency branch untested because
PaperZD exists under every engine. `Paper2DPlus.Build.cs` therefore recognizes the exact environment
override `PAPER2DPLUS_FORCE_NO_PAPERZD=1`. Under that override it skips both filesystem probes, omits
the `PaperZD` module dependency, and defines `WITH_PAPERZD=0`. With the variable unset—or set to any
other value—the normal auto-detection behavior is unchanged.

Use the guarded standalone wrapper for the Fab proof:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-paper2dplus-plugin.ps1 `
  -EngineRoot 'Z:\Unreal\UE_5.8' `
  -PackageRoot 'C:\Users\bluey\p2dp_xver\optional-absent' `
  -PackageName 'Paper2DPlus-UE5.8-NoPaperZD' `
  -ForceNoPaperZD
```

The wrapper scopes and restores the environment, permits recursive cleanup only for a strict child
of the explicit package root, and fails if the UBT log does not contain the forced-absence proof
marker. It also lets BuildPlugin stamp the packaged descriptor for this engine, then requires the
packaged `EngineVersion` to equal the engine's `Major.Minor.0`; `-Unversioned` is deliberately not
used. A successful result JSON therefore needs `engine_version_verified: true` as well as the
forced-absence marker and `release_content_verified: true`. Use a fresh BuildPlugin output for this
check; do not infer optional support merely because the source `.uplugin` marks PaperZD optional,
and do not manually restamp an archive after the audit.

The shared identity seam is:

- `FFlipbookIdentity::Flipbook` — the Paper2D flipbook both systems ultimately render;
- `FFlipbookIdentity::PaperZDSequence` — the optional matching PaperZD sequence;
- `FFlipbookTagMapping::Entries` — ordered flipbook-name plus optional PaperZD-sequence pairs.

Every scalar `TObjectPtr` in that bridge, including deprecated migration fields, has an explicit `= nullptr` member
initializer. UE 5.0 can otherwise leave the storage of a stack-built reflected struct untouched until transaction
serialization resolves its object properties, making an initialization bug appear to be a transaction or editor crash.

### Cross-version proof boundaries

The forced-no-PaperZD BuildPlugin run above proves that the optional dependency is omitted and the distributable package
still compiles. The ordinary guarded BuildPlugin matrix remains the authority for per-engine compilation, release
contents, and descriptor stamping: AutomationTool must stamp each isolated package to the compiling engine's
`Major.Minor.0`, and the result JSON must report matching `expected_engine_version`/`packaged_engine_version`,
`engine_version_verified: true`, and `release_content_verified: true`. The source descriptor remains stamped for the
primary development engine; do not pass `-Unversioned` or manually restamp an archive.

Behavioral compatibility is a separate gate. Run the repository host-runtime matrix against one frozen candidate:

```powershell
$SourceFingerprint = '<frozen 64-hex source_candidate_sha256>'
& 'C:\Users\bluey\Documents\Unreal Projects\LostRadience\scripts\run-paper2dplus-host-runtime-matrix.ps1' `
  -Versions @('5.0','5.1','5.2','5.3','5.4','5.5','5.6','5.7','5.8') `
  -ExpectedSourceFingerprint $SourceFingerprint
```

The wrapper replaces the older external `run_versions.ps1` as release authority. It uses one guarded host sequentially,
proves the normalized source copy and source fingerprint at every checkpoint, runs the full discovered null-RHI suite,
reconciles the completion log with a fresh `index.json`, requires the six exact Frame-Cue/Effect migration tests, rejects
crashes, and proves cleanup. Its disposable host copy may remove `EngineVersion` so each installed editor can load the
plugin; that normalization is not a distributable descriptor stamp and does not replace the BuildPlugin audit.
Renderer-dependent appearance behavior remains the separate real-RHI Profile200 gate. These contracts define what must
pass; they do not imply that a currently running nine-version matrix is complete.

## Recommended workflow

1. Import the source art with Paper2DPlus. Preserve the generated flipbooks as the common animation
   assets used by both systems.

2. Open the Character Profile and author the per-frame data. The compact animation picker above the
   main tools is the normal way to switch animations; open the optional Navigator only when a pinned
   searchable catalog is useful. Author hitboxes in **Hitbox Editor**, timing in **Frame Timing**,
   behavior-carrying placements in **Frame Cues**, and movement in **Root Motion**.

   If an animation needs facing-specific art, configure its Directional Set in Animations and assign
   slots through the shared header wheel. All six Profile tools preview the selected variant while
   continuing to edit the base entry's hitboxes, root motion, curves, transitions, tags, and Cues.

3. Link the profile's animation entries to their PaperZD sequences where PaperZD is installed. For
   a sheet-based character workflow, select a linked Character Profile in the Bulk Extractor and
   open **PaperZD Sequences** beside **Organize Folders**. Choose or clear the Anim Source, Re-scan
   existing matches, and leave **Create and link missing sequences after this extraction** enabled
   when desired. The post-commit creator sees only flipbooks successfully produced by that Extract
   All run; skipped and failed sheets are excluded. It asks for editable names, applies the profile
   prefix once, reuses only a compatible same-name asset, and never overwrites a manual sequence
   reference. The stored `UObject` bridge and `UPaper2DPlusPaperZDLibrary` keep PaperZD types out of
   the public Paper2DPlus API.

4. Build the PaperZD AnimBP over those same flipbooks. PaperZD remains the only owner of playback
   transitions. Paper2DPlus transition arrows can inform the graph or game logic through
   `GetAnimationTransitionInfo` and `ResolveAnimationTransition`, keyed by the Profile plus the current flipbook alone (no group or chain-start selection), but they do not switch animation. Feed the resolved flipbook to `GetCachedPaperZDSequenceForFlipbook` when PaperZD playback needs its sequence; there is no tag/index-based PaperZD lookup.

   For multidirectional art, call `ResolveDirectionalFlipbook` first with the Profile, logical base
   (or any owned variant), and non-zero facing vector. Feed the successful flipbook into the
   project's sequence mapping. An occupied set returns `DirectionUnoccupied` for an exact empty
   sector—no nearest or base fallback—so the AnimBP can choose its own explicit policy.

5. On the actor, add `UPaper2DPlusCharacterProfileComponent`, assign the Character Profile, and call
   `SetFrameCuePlaybackSource` with the live Paper flipbook component PaperZD drives.
   `UPaper2DPlusFlipbookComponent` is the recommended render component because its sender-bearing
   flipbook/frame/start/terminal delegates provide synchronous event-driven updates. A stock
   `UPaperFlipbookComponent` remains supported through fallback observation: a Profile or equipped
   Layer carrying any Frame Cue is sampled every frame, while a cue-free actor keeps the 20 Hz idle
   watch.

6. If PaperZD owns the playback session, call
   `BeginExternalFrameCuePlayback(PlaybackComponent)` once when that session begins—even for a
   same-flipbook restart—and retain the returned positive generation. PaperZD may keep the render
   component stopped and advance it with `SetPlaybackPosition`, so `IsPlaying` is not a session
   identity. After the definitive final frame call
   `ReportFrameCuePlaybackCompleted(PlaybackComponent, Generation)`; after a manual/cancelled stop
   call `ReportFrameCuePlaybackStopped(PlaybackComponent, Generation)`. Both reports are idempotent,
   reject stale source/generation pairs, and finalize Cue lifecycle only; they never stop, start, or
   otherwise take ownership of PaperZD.

7. Bind gameplay to `OnFrameCue` or use the cancellable **Listen for Frame Cue** Blueprint node.
   Filter by Cue class and/or `Paper2DPlus.Cue.*` tag, then perform the requested behavior in the
   receiver. Cues emit `Trigger`; Cue States emit `Begin`, optional `Update`, and `End` with an
   explicit end reason.

8. If networking is enabled, treat the Cue's `NetPolicy` as the broadcast location, not as the
   behavior itself. Gameplay mutation must use `AuthorityOnly`; replicated-world cosmetics normally
   use `CosmeticOnly`; owner UI/camera work uses `OwnerOnly`. Catch-up suppresses Cue replay while
   rebuilding eligible Cue States.

9. Add `UPaper2DPlusLayerRenderComponent` only when the actor uses a Character Layer asset. Appearance
   selection remains a Blueprint/game concern; the profile continues to resolve hitboxes and Cues for
   the flipbook PaperZD chose.

## Frame Cues versus PaperZD AnimNotifies

Use PaperZD AnimNotifies when the notification belongs to state-machine behavior—for example, a
graph-specific state exit or a notify that exists only inside one AnimBP arrangement.

Use Paper2DPlus Frame Cues when the placement belongs to the reusable character/flipbook data—for
example, a projectile request aligned with an attack hitbox, a gameplay-tag range, or a sound/effect
placement that should remain with the Character Profile and its key-frame mutations.

Do not author the same side effect in both systems. If both carry an equivalent trigger, the project
will receive it twice. Frame Cues also do not call PaperZD notifies, and PaperZD notifies do not drive
the Frame Cue lifecycle; both observe the same animation at different responsibility boundaries.

Paper2DPlus adopts the useful part of a class-notify contract without adopting PaperZD's whole
AnimInstance/player subsystem. `FPaper2DPlusFrameCueContext::PlaybackComponent` is the concrete live
render/attachment source, `CharacterProfile` is the exact base/compiled data source, and
`EvaluationMode` distinguishes runtime playback/catch-up from editor playback, scrub/seek, and
selection preview. `UPaper2DPlusBlueprintLibrary::ResolveFrameCueAnchor` turns that coherent Context
into either:

- **Render Origin** — the live component transform; or
- **Profile Socket** — the authored socket in the unique base Character Profile row matching both
  animation name and flipbook, evaluated at the Context frame through the shared pivot/facing/scale
  geometry.

The resolver returns an explicit result, world transform, and the same component as an attachment
source. Attach with keep-world semantics; the authored Profile socket name is not an Unreal component
socket and must not be passed to Attach or snapped to target. A Runtime Customizable Layer Cue still
uses the base timeline component and base/compiled Profile sockets. Effective Layer-child sockets are
outside this contract and fail rather than changing what Profile Socket means.

Natural non-looping completion evaluates final-frame work before one `Completed` End. An explicit or
observed manual stop emits one `PlaybackStopped` End. Direct `Stop()` is covered automatically on the
event-driven Paper2DPlus component and by stock-component observation, but Unreal exposes no virtual
stop notification: a complete Stop-then-Play between observations is invisible. The owning PaperZD/game
code must use the external generation/report seam for that sequence. First terminal boundary wins, so
a later finish, stop, flipbook change, or teardown cannot double-End an already-cleared Cue State.

### Editor preview uses the same behavior in an isolated world

PaperZD and Unreal animation notifies resolve their preview render component's world while a notify
runs. Paper2DPlus follows the same pattern: during each Cue behavior callback, Cue `self` resolves a
transient `EditorPreview` actor/component/world carrying the exact Profile, live registered Paper
component, flipbook, animation, key-frame snapshot, and editor evaluation mode. The same
`ResolveFrameCueAnchor` function and geometry run there, so equal source inputs produce the same world
placement. Spawned actors, Paper2D or particle effects, traces, debug drawing, and sounds therefore
appear in the Frame Cues viewport without creating or registering a second preview Blueprint.

The world is intentionally not the open level and does not provide the project's GameMode,
GameInstance, networking, navigation, AI, or custom player actor. `Is Editor Preview` lets behavior
avoid those game-specific dependencies. Project Preview Adapters remain an optional editor-only
overlay/approximation extension; matching adapters run after ordinary Cue behavior and never replace
it. Runtime Cue listeners are still not executed by editor scrub. Preview play, scrub/seek, and
Preview Selected report `EditorPlayback`, `EditorScrubSeek`, and `EditorSelectionPreview`
respectively. Pressing Stop uses `PlaybackStopped`; seek/session/compile/tool reset uses
`EditorReset`, and source, animation, or Cue mutation retains its own specific End reason.

## Common questions

**Do I need PaperZD?** No. Paper2DPlus can read flipbook changes directly and the game may choose
animations itself. There is no fallback transition driver or phase-group playback system inside
Paper2DPlus; standalone projects own that decision in Blueprint/C++.

**Can Paper2DPlus replace a PaperZD state machine?** No. The Animation Map and transition arrays are
authoring/query data, not a playback controller.

**Does a Directional Animation Set create PaperZD sequences?** No. It stores Paper flipbooks and
returns one through a cooked pure query. Sequence creation, mapping, state selection, and playback
remain project-owned.

**Can PaperZD replace the Character Profile?** Only if the project is willing to rebuild the profile's
hitbox, root-motion, Cue, curve, import, validation, and layered-character pipeline itself.

**Can the systems disagree about the frame?** The profile keys data by Paper2D key-frame index. Keep
both systems on the same `UPaperFlipbook`, and never substitute timeline/sample frame numbers for
key-frame indices.

**Does Frame Cue behavior get a PaperZD AnimInstance?** No. The live Paper flipbook component is the
spatial and attachment authority, and the exact Character Profile plus frame snapshot supplies authored
geometry. This keeps the same Cue Type usable with PaperZD installed, with PaperZD absent, and in the
isolated editor preview.

**Does an effect-carrying Cue query an Effect Profile at runtime?** No. A designer-authored Cue Type
uses ordinary variables and behavior; scalar soft Paper Flipbook variables are warmed structurally
before traversal. Effect Profiles remain tagged visual libraries for explicit authoring and discovery
queries, not Cue-dispatch authority. Old profile/name fields exist only as retained legacy data.

## See also

- [Authority contract](./authority-contract.md) — Cue networking, animation advisory, and game-owned responsibilities.
- [Designer guide](./designer-guide.md) — task-oriented entry point for Character, Layer, Effect, and Combat assets.
- [Multidirectional animations](./directional-animations-guide.md) — Profile storage, radial authoring, resolution results, and deferred scope.
- [Frame Cues guide](./frame-cues-guide.md) — authoring, receivers, automatic behavior preview, optional adapters, and migration behavior.
- **Paper2D key frame versus timeline frame:** `UPaperFlipbookComponent::GetPlaybackPositionInFrames()` returns the timeline frame index (accumulated time × FPS), not the authored key-frame index — these diverge whenever a key frame has `FrameRun > 1`, which Aseprite `GcdExact` imports routinely produce. Index any per-key-frame array (root motion, hitbox frame data, Frame Cue anchors, auxiliary frame curves) with `Flipbook->GetKeyFrameIndexAtTime(FBComp->GetPlaybackPosition())` instead. The reverse holds too: `SetPlaybackPositionInFrames()` takes timeline frames, not key-frame indices, so seeking to an authored key frame means converting the key index to a time and calling `SetPlaybackPosition()`.
- **Blueprint API reference:** the callable Blueprint surface is spread across several libraries. `UPaper2DPlusAnimationMapLibrary` carries exact-tag animation lookup, Chain Start/Chain End combo-chain queries and flipbook-keyed transition inspection/resolution (`FindAnimationByExactTags`, `GetComboOpenerFlipbooks`, `GetAnimationTransitionInfo`, `ResolveAnimationTransition`, `GetComboChainFlipbookAtIndex`, `GetComboChainFlipbookAtIndexByTags`, `GetComboChainLength`, `GetComboChainLengthByTags`, `HasComboChain`); `UPaper2DPlusPaperZDLibrary` carries the PaperZD bridge nodes (`FindPaperZDSequenceForFlipbook`, `GetCachedPaperZDSequenceForFlipbook`, `GetActorCurrentPaperZDSequence`); `UPaper2DPlusDirectionalAnimationLibrary` carries the direction-set nodes. Beside those sit the Frame Cue listener surface (`UPaper2DPlusCharacterProfileComponent::OnFrameCue` and the Cue receivers described above), the Effect Profile and Character Catalog query nodes, the Combat Profile query and mutation nodes, and the runtime appearance nodes (mostly server-authoritative mutations such as Set Skin, Add Wearable and Set Outfit, with Get Active Skin and Get Appearance Descriptor as the queries) plus the `UPaper2DPlusAppearanceLibrary` discovery and swap helpers. The full inventory lives in the LostRadiance host repository's Blueprint API reference.
