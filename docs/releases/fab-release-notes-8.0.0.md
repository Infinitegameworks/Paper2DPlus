# Paper2DPlus 8.0.0

Paper2DPlus 8.0.0 is a major release. It replaces the frame-effect model with behavior-carrying
Frame Cues — Cue Types you author in a restricted Blueprint editor, placed on animation frames,
running their own On Cue Triggered / Begin / Update / End events — and ships one native cue, the
Spawn Flipbook Cue, with a draggable offset gizmo in the preview viewport. Every moment Cue can now
fire at the start or the end of its frame via a draggable trigger-edge diamond, and the Cue Type
save/compile flow was rebuilt so confirmed schema changes commit through any successful save. Around
the cues sits a full designer workspace overhaul: a merged Animations tab with a node-graph
Animation Map, a rebuilt Character Layer appearance model, a direct-manipulation Character Catalog,
and Combat and Effect Profile editors that share one workspace grammar.

The last version published here was 6.2. This release contains two majors' worth of changes (7.0.0
was cut internally but never shipped to Fab), and several of them are breaking. Read the upgrade
notes before opening an existing project with 8.0.0.

## Upgrade notes — read before opening your project

**First: while still on 6.2, open and resave your Paper2DPlus assets, then upgrade.** 6.2's
load-time bridges fold legacy-format values (per-frame hitboxes, excluded frames, root motion,
PaperZD links, effect rows, combat variables) into their current fields on resave. 8.0.0 has
removed the legacy effect and Frame Event conversion bridges outright, and Unreal silently ignores
deleted fields on load — values never folded forward by a resave cannot be recovered afterward.

### Frame Cues

- The six former built-in Cue classes — Play Sound, Spawn Effect, Spawn Projectile, Camera Shake,
  Screen Flash, Apply Gameplay Tag — are deleted with no migration and no CoreRedirects. Existing
  placements of them go inert: never dispatched, never broadcast, reported by validation. Re-author
  each as a Cue Type of your own (the behavior hooks are exactly the On Cue events), or as a Spawn
  Flipbook Cue placement for one-shot flipbook effects.
- Cue objects now run their own behavior, immediately before the `OnFrameCue` listener broadcast.
  This reverses the earlier "cues are data, receivers own behavior" rule. Behavior executes on the
  shared asset-owned placement, so it must stay stateless — persistent state and every release
  belong on the receiving actor. Existing listener-based setups keep working unchanged.
- The public timing classes are renamed: `UPaper2DPlusMomentCue` is now `UPaper2DPlusCue` (**Cue**),
  `UPaper2DPlusRangeCue` is now `UPaper2DPlusCueState` (**Cue State**), and the base is
  `UPaper2DPlusCueBase`. Saved assets load through plugin-local CoreRedirects; C++ and Blueprint
  code naming the old classes must be updated.
- Legacy per-frame Effects rows and old executable Frame Event data are no longer auto-converted to
  anything. The data is retained in your assets, inert. Custom Blueprint Frame Events are never
  dispatched; re-author them as Cue Types with the typed listener.
- Actors whose Character Profile or equipped Layer carries any Frame Cue are watched every frame
  instead of at 20 Hz (roughly 3x the detection ticks for those actors); cue-free actors and the
  event-driven `UPaper2DPlusFlipbookComponent` path are unchanged. An animation whose cues cannot
  dispatch now says why, on screen in development builds.
- For scripted callers: new Cue Types are placement-ready at creation and default to Cosmetic Only
  net policy (not Local Always). Editing a Cue Type's class defaults silently rebases every saved
  placement that equals the old default — a default that must change needs a versioned migration.

### Animation Map and transition queries (Blueprint hard break)

- The tag/group query family on the Character Profile asset is deleted without deprecation:
  `GetFlipbookDataForTag`, `GetFlipbooksForTag`, `GetFirstFlipbookForTag`,
  `GetRandomFlipbookForTag`, `GetPaperZDSequenceForTag`, `GetTagMapping`, `HasTagMapping`,
  `GetFlipbookCountForTag`, and the label-keyed transition API — `FindTransition`,
  `GetTransitionTags`, `GetTransitionTarget` — plus the pure transition queries. Transitions are
  now pure From→To rows. Affected nodes appear broken on load and must be rewired to the Animation
  Map library: `FindAnimationByExactTags`, flipbook-keyed `GetAnimationTransitionInfo` /
  `ResolveAnimationTransition`, and the combo-chain nodes. Old CoreRedirects for the pre-rework
  names were also removed.
- Phase groups are removed entirely. `GetFlipbooksInPhaseGroup` is deleted and frame-data exports
  drop the Group column. Phase identity is the per-animation Phase Tag plus the Animation Map's
  chain-derived phases (Derive Missing Phases).
- The "required tag mappings" project setting and `GetUnmappedRequiredTags` are deleted.

### Character Layer appearance (schema v5)

- The Layer asset was rebuilt around stable Layers, optional Exclusive Groups, complete Appearance
  Presets, and one required Default Appearance, delivered either Fixed / Baked or Runtime
  Customizable. Pre-v5 selection data — including 6.2-era whole-body Variants — is discarded on
  load with a Warning log naming the asset; there is no migration executor. Re-author selections in
  the Layer Workspace.
- The old wearable swap API on `UPaper2DPlusLayerRenderComponent` (`SetActiveVariant`,
  `SetLayerVisible`, `AddWearable`, `SetOutfit`, and relatives) is replaced by the
  `Paper2D+|Appearance` descriptor surface, and its three frame-event delegates are replaced by the
  single `OnFrameCue` delegate.

### Hitboxes

- Hitbox Damage and Knockback are floats (previously integers). Saved assets convert automatically;
  Blueprint graphs wired from these members into integer inputs need the affected pins refreshed.
  Existing Fixed / Baked Layer output reports Needs Bake once after upgrading — re-bake to clear it.

### Editors and tooling

- The Character Data editor is replaced by the Profile Tools window (Content Browser action,
  **Asset → Profile Tools…**, or `Paper2DPlus.OpenProfileTools`; the old console name still works).
  The Tag Mappings surface is retired — chain authoring moved into the Animations tab — while the
  underlying `TagMappings` data survives unchanged.
- The Character Catalog is direct manipulation: characters enter through **+ Add Characters…** and
  leave through explicit removal that stays removed. Project scanning, Scan/Apply, link modes, the
  content-roots setting, the sync commandlet/script, and the separate Groups tab are gone; Groups
  are a rail inside the Roster. Runtime Blueprint Catalog queries are unchanged.
- Animation Map Fragments are deleted outright. The feature was never usable in any shipped
  version; the classes existed in the 7.0.0 binary but had no reachable surface.
- C++ integrators: several editor-module symbols changed or were removed (the Cue picker's
  `Refreshing` status, curve-track mode/action delegates, two Details panel-id renames). See
  CHANGELOG.md for the exact list.

## What's new

### Frame Cues

- Behavior-carrying Cues and Cue States: author payload variables in a restricted Blueprint editor
  (stock My Blueprint panel, protected Compile, guarded envelope) and implement On Cue Triggered or
  On Cue Begin / Update / End directly on the type. Receivers still get the typed `OnFrameCue`
  broadcast after behavior runs.
- Spawn Flipbook Cue: a one-shot, self-destroying flipbook effect at the render origin or a Profile
  Socket, with offset, rotation, scale, tint, play rate, flip-with-character, and optional
  attachment. Selecting a placement shows a ghost of the effect in the preview viewport with a
  translate gizmo — drag it to author the offset, one drag per undo entry.
- Trigger Edge: drag the anchor diamond to either side of a cue's frame cell. Frame Start is the
  legacy default; Frame End fires when the frame's trailing boundary is genuinely crossed —
  including natural animation completion — and never fires on a frame cut short.
- Cue behavior previews automatically in an isolated editor world: spawned actors, effects, traces,
  and sounds run on scrub, playback, and Preview Selected with no adapter registration. Failures
  badge the placement and never quarantine it.
- The save/compile flow tells the truth: a confirmed destructive schema change commits through any
  successful save (Save All and Content Browser saves included), refused saves state their reason
  on screen, and the compile gate distinguishes an invalid authored schema from an unusable durable
  baseline instead of blaming the wrong one.
- New Cue Types appear in **+ Add Cue** immediately; types that cannot be placed show as disabled
  rows carrying the reason instead of hiding. Placements get readable timeline identity from
  authored colors and the project Tag Colors registry.
- Exact terminal semantics: every begun Cue State receives one concrete End reason (Completed,
  Interrupted, Animation Changed, and so on), natural completion samples the final frame first, and
  manually driven playback — PaperZD included — dispatches with zero integration code.

### Animations workspace

- One Animations tab with Grid, List, and Map views. The Map is a node-graph over your authored
  transitions: chain start/end markers, chain tags, derived combo numbering, bulk group/phase/tag
  edits, comments, and a tag filter.
- A nine-node Blueprint surface keyed by flipbook or exact tags: exact-tag lookup, flipbook-keyed
  transition inspection/resolution, combo-chain stepping by index, chain length, opener discovery,
  and `HasComboChain`.
- Expected Tags: Character Catalogs declare expected animation tags per roster and per group; the
  profile editor shows a permanent coverage checklist and closes gaps by picking an animation or
  dragging the tag onto one.

### Character Layers and runtime appearance

- Schema v5: stable Layers, optional Exclusive Groups, complete Appearance Presets, one Default
  Appearance; Fixed / Baked publishing with transactional bake, recovery, and exact source-pixel
  reproduction, or Runtime Customizable delivery with a replicated descriptor, deterministic
  gameplay composition, an opt-in hybrid GPU compositor, and a world budget subsystem for crowds.

### Combat, Effects, Catalog

- The Combat Profile editor mirrors the Character workspace: attack cards, a contextual inspector
  with live rank and score, layered scoring rules, and a Score Playground that survives edits and
  undo. Scoring remains advisory — the game owns playback and damage.
- Effect Profiles remain a fully supported tagged visual library with lazy soft-reference art
  loading; Blueprint callers still receive ordinary hard flipbook outputs.
- The Catalog roster shows real authoring progress per character (manual checklists exported as
  registry tags — no checklist data is an explicit state, never silently complete), plus
  Suggest Companions and roster-wide expected-animation coverage warnings.

### Runtime

- Automatic, event-driven hit detection (opt-in): authored attack boxes arm detection windows,
  hits dedup per victim per move instance, and `OnHitConnected` / `OnHitReceived` /
  `OnAttackWhiffed` broadcast with full payloads. The plugin never applies damage.
- Explicit hit-stop API, float damage/knockback, opt-in server-authoritative replication of
  anim-state / combat / hit-stop / wardrobe intent, and an opt-in profile-authored relative
  transform apply.

### Import and extraction

- The bulk extractor reads frame grids off the pixels (validated at zero sliced sheets across 462),
  places padded cells midpoint-to-midpoint so vertical motion survives, keeps trim opt-in, moves
  de-bake to its own window, and adds JSON export/import of the whole folder organization. The
  single-sheet extractor gains fail-closed grid auto-detect.
- Aseprite import reproduces exact per-frame durations, delivers name-convention hitboxes with a
  conflict dialog, and reimports additively instead of overwriting. The Flipbook Draw tool edits
  pixels directly on flipbook frames with symmetry, onion skin, and undo.

## Fixes of note

- A sideways sprite offset no longer walks the character away from its capsule on facing flips
  (the wallslide drift).
- PaperZD-driven characters no longer go silent after one observed stop: manually driven playback
  reopens cue dispatch automatically.
- Cue Type saves no longer fail into a reasonless Retry loop, and destructive-change confirmations
  are honored by every save path.
- Catalog drag-reorder can no longer rewrite authored order it was not aiming at under an active
  filter.
- Loop-spanning Cue States no longer End+Begin on every loop pass.
- Flipbook Draw edits re-derive sprite bounds, so edited frames no longer render cropped and
  offset.
- The release gates caught two host-only defects before shipping: a UE 5.0–5.4 assert in the new
  shared profile toolbar's PIE group (cross-module command registration, fixed via the engine's
  adoption path) and two preview-audio tests failing falsely on audio-disabled machines (now
  honestly gated on audio availability).

## Compatibility

- Supported engines: Unreal Engine 5.0 through 5.8, one package per engine version.
- Paper2DPlus follows semantic versioning; 8.0.0 is a major with the breaks listed above and no
  others intended. The full, precise record — including every removed symbol — is in the plugin's
  CHANGELOG.md.
- Assets saved by 8.0.0 should not be expected to open correctly under older plugin versions.
  Upgrade project-by-project, and resave on 6.2 first.
