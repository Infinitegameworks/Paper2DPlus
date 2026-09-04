# Changelog

Versioning rules and the release checklist live in [RELEASING.md](RELEASING.md).

## Unreleased

_Nothing yet. The next version is chosen when the first entry lands (MINOR for additive work; a
**BREAKING** entry makes it MAJOR) — see [RELEASING.md](RELEASING.md)._

## v9.0.0 — 2026-09-04

### Added

- **`Resolve Animation Transition` has a `Success` output.** The Blueprint node returns its result enum
  unchanged and additionally sets a trailing boolean that is true exactly when the result is `Success`, so a
  graph can branch on one pin without comparing the enum; the enum still says why a resolve failed.
  Additive: existing pins keep their names and order, placed nodes gain the new pin unconnected.
- **Dangling sprite references are a validation Error.** Every sprite reference recorded on a Layer Profile's
  layers must resolve to an asset that exists (checked against the Asset Registry, nothing is loaded); a
  layer with missing frames produces one Error naming the layer and the missing count in every Validate
  surface (the Validate action, the Content Browser action, the validation panel and the JSON commandlet).
  Every `.ase` import and replay runs the same check on the asset it just wrote and reports it in the
  import notification and the Output Log - the importer no longer returns a silent success while the
  asset points at art that was never created.

- **Incremental .ase reimport: a save regenerates only what actually changed.** Every generated
  asset class now carries a write gate — per-layer sheets on their pixel hashes, paired normal
  sheets on a new per-normal stamp, the composited sheet on a whole-composite hash, flipbooks on a
  structure hash over range + per-frame durations + keyframe sprites, profile rows by direct
  comparison — with existence checked per asset through the Asset Registry, so a deleted generated
  asset is re-created rather than skipped, and absent stamps (imports that predate this) always
  rebuild rather than skip. Sprites ride their sheet's verdict: an unchanged layer's sprites are
  not even loaded, and a changed layer's sprites rebuild only when their serialized tight bounds
  provably moved (derived from the new pixels, compared against the stored geometry — the stale
  tight-bounds crop bug stays closed by proof instead of by brute force). A pixel edit confined to
  one layer now rewrites that layer's sheet and the composited sheet; a pure retime rewrites
  exactly the affected flipbook; a byte-level change with no asset effect dirties exactly one
  package — the Layer Profile, restamped so the startup reconcile never re-queues it. Every skip
  decision is logged with its reason, and each auto-reimport publishes a walkable, non-modal
  **Aseprite Import** message-log audit page naming every decision.
- **Force Full Reimport** on the Layer Profile's Content Browser menu: replays the complete import
  for every tracked source with every gate bypassed — the recovery path when a skip looks wrong. A
  manual action is explicit user intent, so it also works while Live .ase Auto-Reimport is off.
- Generated sprites now keep their `PixelsPerUnrealUnit` across reimports (it is assigned only when
  a sprite is first created), and skipped-but-resident sheets get drifted pixel-art settings
  (filter, mip, compression, sRGB, streaming, LOD group) repaired on every pass.
- **Live .ase Auto-Reimport can be turned off** (Project Settings > Plugins > Paper2DPlus >
  Aseprite Import). While it is off, saves and pulls of tracked `.ase` files are detected but
  deliberately not reimported — one log line says so — and non-`.ase` texture reimport is
  unaffected. Turning it back on immediately reconciles by content hash, so every file that
  drifted while it was off reimports through the standard pipeline without an editor restart; the
  same reconcile still runs on editor start. The default (on) preserves today's behavior.
- Every `.ase` auto-reimport now logs a one-line cost report — per-asset-class written/skipped
  counts, unique packages dirtied, and a parse/composite/texture-build/sprites/flipbooks/profile
  timing split — so a reimport's cost is diagnosable from the editor log alone and the incremental
  reimport work can prove what it saves.
- **Per-slot Mirror flag for directional slots.** Each occupied slot row in
  Details > Directional Animation gains a **Mirror** checkbox, so a standard 8-way set can ship
  five authored facings and reuse three mirrored — the wheel labels such slots "(mirrored)".
  **Resolve Directional Flipbook** and **Get Occupied Direction Slots** now return the flag beside
  the art (a new Boolean output pin / struct field — additive, existing graphs keep working);
  applying the horizontal flip stays project-owned (typically actor scale), so the plugin never
  mutates render state. Re-picking a slot's art preserves its mirror flag, and the flag
  round-trips through JSON export/import.
- **Per-slot directional rows in Details > Directional Animation**: one details-style row per
  active slot (compass label, bearing, and a standard soft-asset picker with drag-and-drop and
  clear), so a Directional Set is visible and editable as a list — the wheel is an accelerator,
  not the only door.
- **Auto-fill from names…** fills every empty active slot from flipbooks named
  `<Base>_<N/NE/E/...>` or `<Base>_<slot index>` (separators `_`, `-`, space; case-insensitive) in
  the base flipbook's folder, with a reviewable proposal and one undoable transaction. Ambiguous
  matches are skipped, never guessed; occupied slots are never overwritten.
- New cooked Blueprint nodes: **Get Direction Settings** (pure; effective count/offset/presence),
  **Get Occupied Direction Slot Indices** (pure; load-free occupancy), **Resolve Direction Slot
  Index** (pure; the resolver's exact angular math for any topology), and **Make Direction From
  Bearing** (pure; states the clockwise-from-up convention as one node).
- The Character Profile Component async-preloads the current animation's occupied directional
  variants when its animation cache warms (released on the next animation change), so a facing
  change during play no longer stalls on a synchronous load. Dedicated servers skip the warm.
- **Runtime customization Blueprint library** (`UPaper2DPlusAppearanceLibrary`, category
  **Paper2D+ | Customization**). The Layer Render Component's mutators all take Layer ids, but no
  Blueprint call returned those ids beside their names, so a character-creator UI could not
  enumerate what a character offers or step to the next option. **Get Layer Options** / **Get
  Appearance Slots** / **Get Appearance Preset Options** describe a Layer asset (id, name,
  Exclusive Group, active state, paint order) before any actor exists; **Describe Layers** /
  **Describe Appearance Slots** / **Describe Appearance Preset Options** do the same for a live
  component; **Cycle Appearance Slot (By Name)** steps an Exclusive Group forward or backward with
  an optional wear-nothing position, **Select Slot Option** picks one directly, **Toggle Layer (By
  Name)** flips an independent Layer, and **Cycle Appearance Preset** walks the authored presets.
  Every mutation goes through the component's existing committed setters, so authority gating,
  normalization, gameplay recomposition and replication are unchanged.

- **An Aseprite import can propose its Sections for you.** The per-row **Edit...** window in the Bulk
  Sprite Extractor gains **Preview Section Suggestions...**, which derives one Section per top-level
  folder in that file - a layer's name IS its Aseprite group path, so no re-parse and no disk access
  is involved. Nothing is written until you press Apply.
  - ONE level only: `Armor/Pauldron/Left` joins **Armor**, not an invented `Armor/Pauldron`. A layer
    at the file's root is left ungrouped rather than swept into a catch-all.
  - The proposal is editable before it applies: tick or untick each Section, rename it (the folder
    stays the match key, so reopening the preview keeps your name), or remove it outright. A name the
    Layer Profile already has is flagged and MERGES into that Section instead of minting a duplicate.
  - Apply writes the `LayerGroups` row and every member's `GroupId` in ONE transaction, so one undo
    takes both halves back. Splitting them would leave a `GroupId` with no row, which the identity
    pass that runs on every import silently invalidates.
  - On a first import the layers do not exist yet, so the accepted set is applied the moment that
    file finishes importing and the import summary reports how many layers were placed. Re-applying
    is idempotent: sections resolve by name, a layer moves only if it actually changes, and an apply
    with nothing to do opens no transaction and leaves the package undirtied.
  - Reimport still never re-derives sections. A curated assignment survives, a renamed layer carries
    its Section with it, and a new layer arrives ungrouped.

- **Layers can be dragged in the Structure dock.** Drop a layer onto a **Section** header to change
  which Section it belongs to, or into the gap between two rows to place it precisely in the global
  layer order. An insertion marker shows where it will land, and a drop that crosses a Section
  boundary applies the reorder and the new Section as ONE undo step.
  - A drop onto a Section header carries membership only - the global order is deliberately
    untouched, which is what lets every row honestly display its global position.
  - Dropping onto the **Ungrouped** row takes a layer back out of its Section.
  - A drop that would change nothing (back into the gap it already occupies, onto its own Section,
    onto itself) is refused outright, so it opens no transaction and does not dirty the asset.
  - Drag stays available while a search filter is active: a drop means "immediately before/after the
    TARGET's global position", and every row shows that position, so a filtered gap still names
    exactly one index.

- **BREAKING — Character Profiles now own optional multidirectional animation art (TASK-190,
  2026-08-22).** Each logical animation may contain a sparse, presence-aware Directional Animation
  Set while its base flipbook remains the identity and owns hitboxes, root motion, curves,
  transitions, tags, and Frame Cues. Profiles default to eight evenly spaced slots at zero degrees;
  designers can choose 3–16 slots and -45 through 45 degrees globally or override both per
  animation. The shared Character Profile header opens one accessible radial wheel in Animations,
  Hitbox, Sprite, Frame Timing, Frame Cues, and Root Motion, with click or remappable
  hold-point-release selection and a persistent slot inspector. Cooked Blueprint adds **Has Multi
  Direction**, **Resolve Directional Flipbook**, and **Get Occupied Direction Slots**. Resolution
  accepts the base or any owned variant, matches PaperZD 2.2.4's +Y-zero clockwise sector math, and
  fails explicitly for an exact empty sector—there is no nearest or base fallback after the set is
  populated. The feature works without PaperZD; projects remain responsible for mapping the
  returned Paper flipbook to a PaperZD sequence and for playback. Existing Profiles migrate from
  Character Profile JSON schema 8 to 9 as base-only data; existing proxy animation replication
  remains base-only. Existing assets migrate automatically and should be resaved; consumers that
  parse or produce Character Profile JSON must update for schema 9 and its directional fields.
- **Reimporting a changed `.ase` now diffs it structurally instead of guessing from names.** When a
  layer or animation tag is RENAMED in Aseprite and its pixels are unchanged, the reimport rebinds
  the existing data in place - the layer keeps its stable id, preset membership, exclusive group,
  placement and layer-local gameplay; the animation keeps its combat, timing, cue, transition, tag
  and chain data - and renames the generated sheet, sprites and flipbook on disk through the
  standard redirector so existing references keep resolving. Previously a rename appended a
  duplicate layer and stranded the old one forever.
  - A rename is accepted ONLY when it is unambiguous (exactly one candidate on each side), the
    stamped content hashes are identical, no sibling `.ase` of the same Layer Profile contributes
    that name, and the new name is not already owned. Anything else degrades to delete-plus-add and
    is reported - it never guesses.
  - A layer or animation that disappears from the file but carries authored work (offsets, authored
    animations, exclusive group, preset membership, combat/timing/cue/tag data) is **kept** and
    reported. Only untouched generated rows are dropped, and their backing assets are always left
    on disk and reported as orphans - nothing is ever deleted.
  - A name a sibling source still contributes is kept and reported as owned by that file, with a
    hint to reimport it.
  - The outcome is reported non-modally: an editor notification with counts plus a walkable
    **Aseprite Import** Message Log page naming every decision. The watcher path raises no dialog,
    and a reimport that changed nothing leaves the package undirtied.

- The recommended gameplay-tag taxonomy now ships with the plugin as a tag-ini source
  (`Config/Tags/Paper2DPlusTags.ini`, registered at runtime-module startup via
  `AddTagIniSearchPath`): the `Paper2DPlus.Animation.*` dimensions (Ability, Combat, Context,
  Flavor, Interaction, Lifecycle, Locomotion, Reaction) and the `Paper2DPlus.Phase.*` model appear
  in every consuming project's tag picker with no per-project config copies. Adds seven universal
  tags: `Locomotion.Dash`, `Locomotion.WallJump`, `Locomotion.Hang`, `Context.Injured`,
  `Reaction.Grabbed`, `Reaction.BlockHit`, `Flavor.Defeat`. Projects that previously copied these
  entries into `DefaultGameplayTags.ini` can delete them.
- **Aseprite import dialog now lists the file's animation tags** with per-tag import checkboxes
  (all on by default) — each checked tag becomes its own flipbook, exactly as before, but the
  dialog finally SAYS so. Clicking a tag row scopes the preview scrubber to that animation's
  frame range (click again for all frames), and the summary line reports "N of M animations →
  flipbooks". De-selected tags produce no flipbook/animation mapping; their frames still import
  as sprites. De-selecting every tag falls back to the single all-frames flipbook.
- **Hitbox layer name prefixes are now configurable** (Project Settings → Paper2DPlus →
  Aseprite Import): each row maps a case-insensitive layer-name prefix to a hitbox type.
  Defaults keep the shipped `attack*`/`hurtbox*` convention and add the common generic
  `hitbox*` (imported as Attack), so layers like "HitBox" become per-frame hitbox data instead
  of compositing into the art. An emptied list falls back to those defaults at import time; the
  `socket_<Name>` convention is separate and always active.
- **Multi-file .ase drops can reuse one dialog's settings**: a new "Use these settings for
  remaining files" checkbox imports the rest of the drop with the same Import Mode and Profile
  choice, no further dialogs. Each file still derives its own asset names and imports all of its
  layers and animations.
- **The import dialog's Import Mode dropdown is replaced by asset pickers** (bulk-extractor
  style): a Character Profile picker (None = create/refresh "&lt;prefix&gt;_Profile"; pick an
  existing Profile to deliver hitbox data onto it without regenerating its animations) and a
  Layer Asset picker (None = create/additively-reimport "&lt;prefix&gt;_Layers"; pick an existing
  Character Layer asset to import into it), plus a compact "Separate flipbooks only" checkbox
  for the former per-layer mode. The three underlying import modes are unchanged.
- **"Keep .ase in project" (default on)**: importing copies the source .ase beside its generated
  assets (the output path's disk folder) and the created Layer asset tracks the copy — stored
  PROJECT-RELATIVE, so the artist can commit the .ase with the project and every synced machine
  resolves the same source. No-op when the file already lives inside the project.
- **Artist edits now propagate even when they land while the editor is closed.** Layer assets
  expose their source-.ase path and a content hash as asset-registry tags, so the live-reimport
  watcher maps every saved asset WITHOUT loading it (assets load lazily, only when their file
  changes); once the registry's initial scan completes, a startup reconcile compares each tracked
  file's hash against its last-import stamp and auto-reimports mismatches — e.g. a pulled commit
  from the artist. The watcher also reacts to file ADD events now (git materializes pulls as
  delete+recreate, which the Modified-only filter silently missed).

- **Generated assets are organized into subfolders** (default on, dialog checkbox): flipbooks in
  `Flipbooks/`, sheet textures in `Sheets/`, per-frame sprites in `Sprites/<name>/`; the Profile
  and Layer asset stay at the output root. Previously several hundred sprites landed flat in one
  folder and drowned everything else.
- **Picking a Character Profile now imports INTO it.** An empty (e.g. freshly created) profile is
  fully populated; a populated one gets this file's animations ADDED — same-name entries refresh
  their art while authored combat/timing/tag data and other files' animations stay untouched —
  so several .ase files can build one character's Profile. Previously picking a profile silently
  skipped flipbook creation entirely, which read as "the import made nothing".

- **Aseprite saves now reflect everywhere, near-instantly.** The import stamps its context
  (output path, prefix, organization, tag de-selection) onto the Layer Profile; when the tracked
  .ase changes, the watcher re-runs the FULL import pipeline against the same assets — the sheet
  texture, per-frame sprites, and flipbooks refresh in place, NEW tags become new flipbooks, and
  the Character Profile updates additively. Assets imported before this carry no context and keep
  the older layer-only diff reimport.
- **Both picker rows gained a "New…" button**: type a name, Create, and the Character Profile /
  Layer Profile is created at the output path and selected — no Content Browser modal (which the
  old flow then hid under the import window). An existing asset with that name is selected
  instead. The Layer Profile row is now labeled "Layer Profile" (the Character Layer asset).
- **The .ase source copy lands flat in `SourceArt/`** (no mirrored folder nesting).

### Changed

- **BREAKING: `Get Occupied Direction Slots` is now BlueprintCallable (impure).** The node
  synchronously loads every occupied directional variant, so that cost now reads as an execution
  step instead of hiding behind a pure node the K2 compiler may evaluate once per connected pin.
  Graphs that used it as a pure node must wire it into an exec chain. Load-free occupancy questions
  move to the new pure **Get Occupied Direction Slot Indices**.
- **`Resolve Directional Flipbook`'s `Direction Unoccupied` failure now reports the resolved empty
  sector in its Slot Index output** (previously cleared to -1; the flipbook output stays cleared).
  With the new topology nodes this lets a Blueprint implement its own nearest/base/hold fallback —
  the resolver itself still never falls back. No pins changed; only the value on that one failure.
- **The direction wheel was repainted onto the editor's actual color scheme.** The near-black
  square plate is now a theme-token disc, selection uses the plugin's selection green instead of
  amber (which means Chain Start/warning everywhere else), occupancy is a filled wedge instead of a
  1.25px hairline, hover/keyboard focus are wedge-shaped house-blue/primary overlays instead of
  white rectangles, the standing white frame became a focus-visible primary ring, compass labels
  (N/NE/...) replace bare indices on zero-offset 4/8/16-way wheels, a fixed north tick anchors any
  angle offset, and the hub shows the animation name, live "N of M assigned" coverage, and the
  pointed slot's assignment. Clicking an invalid wedge now refuses in place (the wheel stays open
  with its warning visible); only outside-ring clicks dismiss. The held-shortcut wheel is now
  centered on the cursor correctly at every Windows DPI scale.
- **Directional header chrome is earned, not permanent.** The Direction/inspector/Assign/Clear
  cluster appears only for animations that carry a Directional Set; other animations show a single
  **Add Directions…** entry point. Assign names its exact target ("Assign → NE"), the picker opens
  pre-filtered to the base animation's name with the current assignment selected, every refused
  Assign/Clear raises a notification naming the reason, and the inspector speaks compass ("Jab ·
  NE (Slot 1) · Occupied") with the exact bearing in its tooltip.
- The Details section's profile-wide rows are now labeled **Direction Count** and **Angle Offset**
  under an explicit "Profile defaults" hint (matching the designer guide), and the presence row
  reads "N of M directions assigned".
- **Directional silhouette differences are advisory Warnings, not blocking Errors.** The
  compatibility gate and validator now block only on the trim-invariant frame space — untrimmed
  canvas, pivot position in that canvas, pixels-per-unit, and rotation — which is exactly what
  keeps base-owned hitboxes, sockets, Cues, and root motion spatially correct. Trim rectangle and
  render-bounds differences (which trimmed exports and facing-specific art produce by nature, and
  which previously rejected the Bulk Extractor's own trimmed output) surface as a validation
  Warning instead.
- The **Angle Offset** tooltips now state the actual sign convention: a positive offset buckets
  the incoming facing as more clockwise, rotating the sector layout counter-clockwise on screen —
  sign-compatible with PaperZD's Directional Angle Offset (the math is deliberately identical).
- **The Bulk Sprite Extractor's Aseprite batch is configured in one place.** The full-width `.ASE
  BATCH` bar above Extract All is gone; its Layer Profile picker, the two options and the fork
  confirmation moved into an **ASEPRITE IMPORT** section in the right pane directly under the ONE
  Character Profile picker (the bar had duplicated it, with a different "new profile" flow). The
  section also shows the **Output folder** the import writes to, with a browse button — previously
  reachable only through Organize Folders, so a batch loaded from the Tools menu silently targeted
  `/Game`. Both profiles' **New…** popups now name the destination package live, turn into
  **Select** when that name already exists, and toast what they did; the engine's Save-Asset-As
  modal (which opened under the tool window) no longer appears here. The right-pane texture
  sections, Auto-Pad and the texture-only status filters collapse for an `.ase`-only batch when
  nothing is selected, the list pane is wide enough to show names (long ones ellipsize with the full
  name in the tooltip), and the window says "Import" for a batch of files: title **Import Aseprite
  Files**, list header **ASEPRITE FILES**, button **Import All**, "No file selected".

- **BREAKING: the public header `LayerAuthoringWorkspace.h` is DELETED**, along with
  `SLayerAuthoringWorkspace`, `FLayerWorkspaceResponsiveState` and `ELayerAuthoringMode`. That widget
  was the Layer editor's embedded mode-switch surface before the docked-tab layout; it has been
  unreachable from production ever since, because `FCharacterLayerAssetEditorToolkit` spawns Art,
  Hitboxes, Frame Cues and Appearance as real tabs beside a persistent Structure dock.
  - **What a consumer has to do:** nothing for assets, and nothing for any supported workflow — the
    widget could not be opened. C++ that included the header for `ELayerAuthoringMode` has no
    replacement enum, because tab activation is the mode switch now.

- **BREAKING: the public header `AsepriteLayerImportDialog.h` and the `SAsepiteLayerImportDialog`
  class are DELETED.** The modal dialog was retired from the import path when Aseprite files became
  batch sources; its authoring surface now lives in the Bulk Sprite Extractor's per-row **Edit...**
  window as `SAseRowImportEditor` (`AseRowImportEditor.h`), and its composited preview widget moved
  out intact as `SLayerImportPreviewCanvas` (`LayerImportPreviewCanvas.h`).
  - `FPerLayerBufferMap`, `EAsepriteImportMode` and `FAsepriteLayerImportSettings` moved to
    `AsepriteImporter.h`, beside the importer that consumes them. `FAsepriteImporter::CompositePerLayer`
    now spells its return type `FPerLayerBufferMap` (the same type, named).
  - `SAsepiteLayerImportDialog::InitDefaultSelection` is gone; it had been a forwarder since the batch
    rework. Call `FAsepriteImporter::InitDefaultSelection` instead.
  - **What a consumer has to do:** nothing for assets. C++ code that included
    `AsepriteLayerImportDialog.h` for the settings types should include `AsepriteImporter.h`; code
    that constructed the dialog has no replacement, because the import UI is no longer modal.

- **BREAKING: Aseprite files are now BATCH sources in the Bulk Sprite Extractor, not per-file
  modal imports.** Dropping a `.ase`/`.aseprite` file into the Content Browser (or **Paper2D+
  Actions -> Import Aseprite Files...**) loads it into the Bulk Sprite Extractor as a source row
  beside any textures, where **one** Character Profile and **one** Layer Profile are chosen for the
  whole batch and a single **Extract All** imports them together. Every file in the batch merges
  into that one Layer Profile instead of minting its own `<prefix>_Layers` - the behaviour that
  made picking an existing profile look like it "made new ones".
  - `.ase` rows join the organizer, batch rename, search, sort and status filters, and carry their
    own status chips (Ready / Parse Error / Imported / Import Failed). They never enter the
    texture-only phases (grid detection, auto-pad, trim, de-bake) - their parsed frame data is the
    layout.
  - Extract All commits textures first and runs `.ase` rows only after that succeeds. A failed row
    keeps its own chip, the window stays open, and re-running retries just the failed rows.
  - The batch bar carries the retired dialog's surviving options: **Separate flipbooks only** and
    **Keep .ase in project**, plus **New...** create-or-select buttons for both pickers.
  - **What a consumer has to do:** nothing for existing assets. Note the engine limit that a
    multi-file Content Browser drop delivers only its FIRST file to the window (Unreal stops its
    per-file import loop as soon as a factory reports a cancel) - use **Import Aseprite Files...**
    to load a whole set at once.
- **BREAKING: the old flat (non-layered) Aseprite import modal is gone.** `FAsepriteImporter::
  ShowImportDialog` no longer opens a window with a typed output path and asset prefix that called
  `ImportFile` directly; it now prompts for one or more files and loads them into the Bulk Sprite
  Extractor. `FAsepriteImporter::ImportFile` itself is unchanged and still available to C++ callers.
- `SAsepiteLayerImportDialog::InitDefaultSelection` moved to `FAsepriteImporter::InitDefaultSelection`
  so the bulk intake, the per-row editor and the live-reimport watcher share one default-selection
  rule. The dialog's forwarder is retained for this release.

### Fixed

- **Force Full Reimport refuses a source whose layers are shared with another source.** A layer name
  present in two `.ase` files of one Layer Profile is ONE row whose sheet holds both files' frames;
  replaying a single source rebuilt that sheet from that source alone, so the other file's frames sampled
  a layout built for a different frame count (wrong frames at the wrong height) and their old sprites
  dangled - with no error anywhere. The replay now fails closed, names the shared layers and the other
  source in a notification and on the Aseprite Import message-log page, and points at the whole-batch
  path (Import Aseprite Files... with every source), which composes shared rows from all contributors.
  Known issue: a per-source replay that rebuilds shared rows from every recorded source is not in 9.0.0.
- **Animation Map: crash (access violation in `SGraphPin::OnMouseEnter`) when the mouse crossed a
  transition pill after undoing or redoing a wire.** A wire drop spawns the edge node inside the
  engine's open "Create Pin Link" transaction, and the engine records every transactional object into
  the open transaction at construction — pinless, marked for deletion — before the shared spawn helper
  could clear the node's transactional flag. Undo restored that snapshot and trashed the pins the
  still-painted pill held; redo resurrected the node as a zombie outside the graph. The spawn helper
  (Animation Map and Clash Graph) now runs with the transaction buffer suppressed for the whole spawn
  and leaves the node non-transactional, so a spawn inside an open transaction records nothing; and the
  pill's rewire grip — the one pin widget the panel's stale-widget invalidation never reaches — refuses
  hover and drag while its pin is not owned by a live graph node.
- **Dropping several `.ase`/`.aseprite` files on the Content Browser now loads ALL of them.** The
  engine's per-file import loop stops at the first factory cancel — which the Aseprite factory has
  to report, because a file is a batch source rather than an asset — so a multi-file drop delivered
  exactly one file. A Content Browser drag-and-drop extender now claims such drops before that loop
  and hands the whole list to the Bulk Sprite Extractor; any non-Aseprite file in the same drop still
  imports normally. Files can also be dropped straight onto the open window (the factory's log line
  had promised this without it being true). The Import button / File > Import path still goes
  through the factory and keeps the engine's one-file limit.
- **An `.ase` layer whose name carries a character the engine forbids in an asset name (`& ! ~ @ #
  . ,`, quotes, brackets) now imports.** Only spaces were sanitized, so such a layer's package could
  never be created: no sheet, no sprites, yet the Layer asset still recorded references to the
  intended paths and rendered nothing without any error. Every engine-invalid character is now
  replaced one-for-one (names the old sanitizer already produced are unchanged), the group folder
  segments get the same treatment, and the import logs a warning naming the substituted asset name.
- **A dropped Aseprite batch selects its first row.** The center pane sat at "No texture selected"
  over black, and the right pane showed GRID / DETECTION / OPTIONS for a batch that had no texture,
  until the user clicked a row: the deferred first-row selection only ran for a window created with
  textures, and the `.ase` path creates the window empty and appends afterwards.

- Pointing the direction wheel at an empty/loading/unavailable sector no longer rescales base-owned
  hitboxes, sockets, and offsets: the Hitbox/Sprite canvas zoom and extent are anchored to the
  canonical base flipbook instead of collapsing to a 128×128 fallback when the directional preview
  has no art (a valid variant is geometry-identical to base by the compatibility gate).
- The Frame Cues frame strip now follows the directional preview like the canvas directly above it
  (it previously kept base art beside a variant canvas and never refreshed on a bearing change).
- Root Motion keeps its grid, ground line, motion path, handles, and frame strip when the bearing
  points at an empty slot — base-owned data did not change, so only the sprite blits are skipped;
  the Sprite and Hitbox tool titles now explain an empty bearing the way Frame Timing, Frame Cues,
  and Root Motion already did.
- A blocked directional count reduction is now actionable: every stranded-assignment row carries a
  **Clear slot** action beside Focus.
- The per-frame directional resolver no longer rebuilds a soft object path per candidate reference
  before its pointer compare (measurable per-call cost on UE 5.0–5.5).
- **An `.ase` import now saves what it generates.** The Bulk Sprite Extractor's `.ase` commit
  created every sheet, sprite, flipbook and profile in memory and saved nothing, so the next
  profile save tripped the reference validator once per unsaved reference, and closing the editor
  left the profile pointing at packages that never reached disk. The import now saves every
  package it dirtied in two passes — art first, then the Character/Layer Profiles that reference
  it — and a save failure keeps the window open and names the packages in the summary
  notification.
- The Bulk Sprite Extractor's GRID, DETECTION and OPTIONS sections collapse while an `.ase` row is
  selected: that row never runs detection and its import reads none of those settings, so they
  were controls that looked available and changed nothing. They return on any texture row.

- **The live-reimport watcher never actually started.** Its initialization was guarded by
  `if (GEditor)` inside the module's `StartupModule`, where GEditor is still null — so the
  watcher (and everything built on it: external directory watches, the offline hash reconcile)
  has been dead code in every session since it shipped. It now initializes on
  `OnFEngineLoopInitComplete`, once the editor exists.
- **Linked ("hold") cels no longer import as blank frames.** The parser's frame compositor read
  the linked-cel source lookup from an array that had just been moved into the parsed-data
  struct, so every linked cel silently contributed nothing. Files using Aseprite's linked-cel
  frame holds now composite those frames correctly on import, reimport, and preview.
- **The import dialog no longer buries every asset in a folder named after the file.** The
  factory passed the new asset's PACKAGE path as the output folder, nesting the whole import
  (and the source copy) one level too deep.
- **Engine dialogs stack above the import dialog now.** The window was flagged topmost, so the
  Content Browser's "Save Asset As" (e.g. creating a Profile to pick) opened underneath it.
- **"Keep .ase in project" copies to `SourceArt/` instead of `Content/`.** A loose .ase inside
  Content tripped Unreal's own auto-import monitor, which prompted to re-import the file the
  moment the import finished. SourceArt/ mirrors the output path, commits with the project, and
  the live-reimport watcher covers it as an external directory.
- **The watcher ignores its own echoes.** A change event whose file content matches an asset's
  last-import hash (the import's source copy, duplicate add+modify pairs) is skipped instead of
  re-running the import the user just watched finish.

## v8.0.0 — 2026-08-10

This cut is a major: the six former built-in Frame Cue classes and Cue Type Effect-field schema are
deleted outright (one NEW native cue — the Spawn Flipbook Cue below — ships in their place), the
remaining public Cue timing classes are renamed, the Character Data Tag Mappings
authoring surface is retired after its live chain controls move into Animations, and the Character
Catalog rework below removes public C++ symbols, a Blueprint-exposed enum, a project setting, an
editor console command, and editor-only asset fields. Keep the level here in step with the entries — see
[RELEASING.md](RELEASING.md).

### Editor

- **Automation-fixture Cue Types no longer appear in the + Add Cue picker (2026-08-09).** The
  plugin's concrete test cue classes now carry an automation-fixture marker that both designer
  pickers filter on, while the placement-authoring Ready gate deliberately ignores it so the test
  suite keeps exercising the production placement path. A fresh project's picker lists only the
  Spawn Flipbook Cue and the project's own Cue Types.
- **JSON import now outers re-created Cue placements to the profile asset (2026-08-09).** Import
  deserialized placements through a raw payload struct, which materialized every instanced Cue
  under the transient package with no transactional flag — tripping the WrongCueOuter diagnostic
  and risking placement loss on save. Imported placements (including stashed cues on excluded
  frames and retained legacy Frame Event shells) are now adopted under the asset with the same
  outer and flags the authoring path uses, after the migration chain so legacy JSON keys are
  covered.
- **Span-accepted hits now sweep the resolved attack frame's boxes (2026-08-09).** Hit validation
  accepted a hit whose span resolved to an earlier attack frame but then queried only the CURRENT
  frame's cached world attack boxes, so a hit landing on a non-attack frame swept empty geometry
  and was rejected (under-registration). Validation now builds the resolved frame's world boxes
  through the same transform context the cache uses and sweeps those; the automatic hit-detection
  path keeps its current-frame behavior by design.
- **NEW — Cues can fire at the START or the END of their frame (2026-08-09).** Every moment Cue
  gains a Trigger Edge, edited by dragging the new anchor diamond to either side of the cue's
  frame cell on the Frame Cues timeline — the diamond sits on the boundary the cue fires on,
  exactly like an anim-notify marker. Frame Start is the legacy meaning (fire on entering or
  crossing the frame) and remains the default, so every existing placement behaves identically.
  Frame End fires when the frame's trailing boundary is genuinely crossed: advancing past it,
  looping over it, or the animation naturally completing on it (delivered from the Completed
  playback terminal, where the final frame provably finished). A frame cut short — manual stop,
  animation switch, teardown — never fires its end-anchored cues. Trigger Edge is placement
  timing like Trigger Frame: it lives on the timeline, is excluded from the durable Cue Type
  schema (no existing Cue Type restages), and the editor preview shares the exact runtime
  boundary predicates, loop-wrap behavior included.
- **NEW — Spawn Flipbook Cue, the one built-in cue, with a draggable preview offset (2026-08-08).**
  `UPaper2DPlusSpawnFlipbookCue` is the single native concrete Cue this release ships (a deliberate
  reversal of "no built-ins" while v8.0.0 is still unreleased): it spawns a self-destroying one-shot
  flipbook effect at the render origin or a Profile Socket plus an authored Offset, with Rotation,
  Scale, Tint, Play Rate, flip-with-character, and optional attach-to-character. The soft Flipbook
  reference warms structurally before the animation plays; the class default Net Policy is
  Cosmetic Only (frozen), so dedicated servers never dispatch or load it. Its unique editor
  affordance: selecting the placement in the Frame Cues tool shows a ghost of the effect in the
  preview viewport with the standard translate gizmo — drag it to author the Offset directly, one
  drag is one undo entry, settled exactly once on mouse-up, capture loss, or tool deactivation,
  and the real effect re-previews where it lands. `UPaper2DPlusEffectFlipbookComponent` returns as
  the self-destroying effect component (bound `OnFinishedPlaying`, looping forced off).
- **A confirmed Cue Type schema change now commits through any successful save (2026-08-08).** The
  destructive-compile dialog already promised the change would be "retained until the asset saves
  successfully", but only the restricted editor's own Save could actually advance the durable
  baseline — Save All, Content Browser saves, and editor-close prompts failed the package write
  with the engine's reasonless "failed to save. Retry?" loop. The compile confirmation now records
  the exact reviewed authored-schema fingerprint (session-local, never serialized); while the
  schema still equals it, any successful save stages and commits the reviewed change, and one more
  schema edit re-arms the fail-closed refusal. A confirmation-consuming commit also refreshes the
  restricted editor's compiled-recovery snapshot keying so a later failed save can never restore a
  baseline older than the saved asset.
- **Refused Cue Type saves now say why on screen (2026-08-08).** Every persist refusal (unreviewed
  schema change, deferred behavior node, unavailable schema service) previously reached the user
  only as the engine's bare failed-save prompt while the reason went to the log. A new
  persist-refusal notification seam surfaces the exact reason and the unblocking action as an
  editor notification, deduplicated across the engine's Retry loop.
- **The Cue Type compile gate no longer misdiagnoses or wedges (2026-08-08).** An invalid authored
  schema (for example a Delay node making the schema indescribable) and an unusable durable
  baseline both landed in one message that always blamed the baseline and demanded "save the
  unchanged type once" — a save the same broken state made impossible. The preflight now carries
  both cause texts, the compile refusal presents the real one, and the unusable-baseline case gets
  an informed-consent escape: compile without a recovery snapshot after an explicit dialog, then
  re-establish the baseline with the next successful save. A compile that fails now reports "fix
  the errors in Compiler Results" instead of the save/cook-oriented "must finish a clean full
  compile" advice.
- **BREAKING — the Character Data editor is replaced by the Profile Tools window (TASK-157,
  2026-08-04).** `FCharacterDataEditorToolkit`, its `CharacterDataEditor_Layout_v3` key, and the
  public `CharacterDataEditorToolkit.h` are deleted. Its surfaces move into a single-instance
  master-detail `SWindow` reached from the Content Browser action, a new **Asset → Profile Tools…**
  entry, and the console seam — `Paper2DPlus.OpenProfileTools`, with the old
  `Paper2DPlus.OpenCharacterData` name retained as an alias so existing scripts keep working. The
  window carries **Character Sizing**, **Sprite Bounds**, **Re-extract**, **Validation**, the
  rehosted **Character Data** pane, and **PaperZD Sequences** when PaperZD is compiled in. Removing
  the toolkit also removes a shipped failure class: a persisted closed-tab state could restore that
  editor with zero docked tabs and no route back (TASK-139), which a window with no layout state
  cannot do.
- **BREAKING — Character Profile validation no longer opens a standalone window.** **Asset →
  Validate Character Profile…** now opens the Profile Tools window on its Validation tool, which
  hosts the SAME shared `SProfileValidationPanel` rather than a fork, so the panel can never be live
  in two places at once for one profile. Layer, Effect, and Combat are unchanged and keep their
  standalone modeless window.
- **Characters can be sized against their gameplay capsule inside the editor (TASK-156,
  2026-08-04).** The Character Sizing tool draws the reference pose against the capsule and a ground
  line and computes a **Fit to Capsule** from the pose's measured silhouette, its pixels-per-unit,
  and the capsule — read off the character class's DEFAULT OBJECT, so no Blueprint, no spawned
  actor, and no PIE. Height is shown in Unreal units and metres. The reference pose is one
  deliberate animation (idle by default, overridable): fitting to max extents across every animation
  would let a single outstretched pose shrink the idle. The sprite is sized by **direct manipulation**: drag its body to move it, drag a corner
  handle to scale it uniformly. A click never dirties the asset, one drag is one undo entry, Esc
  restores the pre-drag transform, and arrow-key nudges collapse a press-and-hold run into a single
  entry.
- **Sprite Bounds is now verification-only, and tells the truth (TASK-157.2, 2026-08-04).** Two
  defects are fixed: an animation where no non-empty frame could establish uniform bounds reported
  every frame **healthy** and discarded its explanation, and a frame whose uniform target cannot fit
  its source region was invisible to any surface asking "is there anything to do here?". Reports now
  carry an `AttentionFrames` count (repairable plus unsupported), every diagnostic reaches the user
  regardless of frame classification, and the scan shows a distinct in-progress state instead of
  reading as a frozen window. It offers no repair action; the fix is Re-extract.
- **Sprites can be re-extracted on demand (TASK-157.1, 2026-08-04).** The complete, rollback-safe
  `FTextureReimporter` path previously had no manual trigger at all — the only way to invoke it was
  to touch the source texture on disk and let the file watcher notice. The Re-extract tool lists
  each animation's eligibility, states plainly when stored detection settings are missing rather
  than failing silently, warns when sprites are shared with another Character Profile, and runs one
  transaction per animation so a failure cannot leave a half-applied profile.
- *Note:* `FProfileSpriteBoundsService::RepairProfile` is **retained**, not removed. Re-extraction
  was compared against it item by item and does not cover it — no pivot baking, no `TrimOffset`
  clearing, no fail-closed shared-sprite or conflicting-bounds refusal, and it zeroes `SpriteOffset`
  where repair preserves it. The verdict is recorded on its declaration.
- **Detail rows share one column across every Paper2D+ panel (TASK-157, 2026-08-04).** The
  Character Profile's Relative Transform rows drop a private 72px label box and a hand-picked 8pt
  font for the shared details row, so the column lines up and drag-resizes with the rest of the
  plugin. The vector and rotator editors stay hand-rolled on purpose: `ISinglePropertyView` asserts
  while constructing those rows on UE 5.0 and 5.1, and the shared row supplies the label/value
  framing rather than the editor widget.
- **The sprite extractor now states pixel dimensions in world units (TASK-156, 2026-08-04).** A
  passive readout in the Auto-Pad confirmation reports a cell's size in pixels, its Unreal-unit
  equivalent, and the pixels-per-unit used to convert. It names that value rather than assuming it:
  extraction hard-codes one pixel per unit, which is a property of this extractor and not of sprites
  in general. Extraction behaviour is unchanged.
- **All four authoring profile editors can now launch and control PIE from their own toolbar
  (TASK-176, 2026-08-03).** Character Profile, Character Layer, Effect Profile, and Combat
  Profile keep Unreal's normal asset toolbar and expose its standard Play/Pause/Resume/Step/Stop
  controls, so designers no longer need to return to the level tab or use shortcuts. Existing
  profile-specific actions and the separately restricted Cue Type toolbar remain intact.
- **The restricted Cue Type editor now has its normal Blueprint toolbar (TASK-162.1,
  2026-08-03).** Compile/Diff, Find/Hide Unrelated, Class Defaults, PIE/debug controls, and a
  real Find Results tab are restored without reopening arbitrary Blueprint surfaces. Compile still
  uses the validated Cue path; Class Settings, Delete Unused Variables, and save-on-compile remain
  hidden, and every compile temporarily suppresses automatic package saving until the durable Cue
  schema transaction has completed.

- **Missing Frame Cue placements are now visible and recoverable (TASK-118.1.3,
  2026-08-02).** When the selected animation contains literal null Cue slots, the Frame Cues
  timeline shows their count without inventing a Cue Type, frame, track, or payload. **Remove all**
  deletes only those empty slots in one undoable transaction, preserves every valid Cue and its
  ordering/track membership, and remains repaired after save/reload. The notice stays neutral
  because an unassigned slot and a placement whose Cue Type was deleted serialize identically.

- **Frame Cue behavior now previews automatically in an isolated editor world (TASK-174,
  2026-08-02).** Each Frame Cues editor owns a transient preview actor, inert Character Profile
  Component, current flipbook, and `EditorPreview` world rendered by the Preview viewport. Preview
  Selected, scrub, and playback run the Cue Type's ordinary behavior there, so spawned actors,
  components/effects, traces/debug drawing, and sounds require no second Blueprint or adapter
  registration. Cue `GetWorld()` is scoped to the synchronous callback and restored afterward;
  paired Cue State End runs before seek/stop/mutation/compile/tool/close cleanup, while loop-spanning
  Cue States retain the runtime's continuous seam. The open level is never used. The existing Preview
  Adapter API remains compatible as an optional advanced overlay/approximation layer that runs after
  behavior.

- **Frame Cue save, first-key playback, and preview rendering regressions are fixed (2026-08-02).**
  Save All and ordinary Content Browser saves now compile and durably stage compatible Cue Type
  payload/behavior edits instead of failing with “schema was not staged”; destructive schema changes
  still require one confirmed Save in the restricted editor. Autosave writes a self-consistent
  recovery copy without advancing the canonical durable baseline or placement readiness. Activating
  Frame Cues seats keyboard focus so the first Space press starts playback, and its viewport again
  shows the standard editor grid while retaining editor primitives, particles, translucency, and
  post-process output. Automation now compiles a real designer Cue Blueprint using Unreal's stock
  Spawn Actor node and proves its visible actor is rendered and cleaned up without a preview adapter.

- **Frame Cue preview scrubbing now renders the exact key frame the editor dispatches, and
  unrelated Blueprint compiles no longer rebuild the preview world (2026-08-03).** The isolated
  preview subject previously seeked by timeline frame, so any animation whose exact Aseprite
  timing import produced FrameRun holds above one rendered an earlier frame than the frame strip
  and Cue dispatch used; it now seeks by summed key-frame time, landing mid-frame so rounding
  cannot resolve onto the previous frame. The Blueprint-compile hard reset is also scoped: it is
  skipped when the compiled Blueprint has no Cue or preview-adapter lineage and no live instance
  of its old classes inside the isolated world, and anything unresolvable still resets
  conservatively.

- **BREAKING: The Cue Type picker's unobservable "Refreshing" status is deleted (2026-08-03).**
  Discovery is synchronous, so the state could never be observed.
  `Paper2DPlusFrameCueEditorAuthoring::ResolveCueTypePickerStatus` loses its leading `bRefreshing`
  parameter and `EPaper2DPlusFrameCueTypePickerStatus::Refreshing` is removed (editor-module C++
  only; no Blueprint or asset impact).

- **BREAKING: The Character Data Tag Mappings surface is retired (TASK-171, 2026-08-01).**
  The secondary editor now uses `CharacterDataEditor_Layout_v3` with its non-closable **Character**
  tab and optional **PaperZD Sequences** tab; the removed tab cannot return through a saved v2
  layout. **Chain Start** and **Chain End** are authored with marker/link gestures inside an exact
  Animation Map group, while **Chain Tags** appear in the selected opener's contextual Animations
  **Tags** pane only when that move resolves to exactly one mapping entry and is a Chain Start.
  The Map overflow retains an undoable recovery route for invalid group keys: populated mappings
  move intact to an unused registered tag, while only empty invalid mappings may be removed. This
  removes an editor surface, not its schema: `FFlipbookTagMapping` / `TagMappings` data survives unchanged and
  remains load-bearing for `GroupImpliedTags`, `Flipbooks[].FlipbookGroup`, the unchanged
  `GetComboChain*` / `GetComboOpenerFlipbooks` Blueprint API, and PaperZD mapping. Effect Profiles
  remain an independent supported visual-library feature and are unchanged.

- **Character Catalog Check Again now reports roster-wide expected-animation gaps (TASK-166,
  2026-08-01).** The existing docked Warnings surface runs the shared coverage resolver for each
  saved Character Profile only after the explicit button press, with one visibly attributed progress
  step per roster row. Every missing or inherited-only expected tag is a Warning naming the character
  and tag; activation reveals the Catalog row and Open targets the Profile. The report schema is
  unchanged, coverage warnings never increase `error_count`, and opening or refreshing the Catalog
  never bulk-loads its roster. If a registry-present named asset cannot load during the explicit pass,
  the audit now fails closed with `Paper2DPlus.Catalog.Validation.AssetLoadFailed` instead of silently
  omitting its native checks.

- **Character Profiles now show and close Catalog-declared animation gaps in one Expected Tags tab
  (TASK-163, 2026-08-01).** The profile-wide tab sits beside Details and renders one row for every
  expected tag, including missing rows, using the shared coverage resolver's covered, qualified,
  group-implied, and chain-inherited provenance. Designers can pick an animation from the row or drag
  the expected tag onto a Grid/List animation row or card; each gesture adds only that exact tag to the
  animation's own `EditorMeta.AnimationTags` in one undoable transaction. Foreign, stale, null, and
  already-authored drops fail closed without dirtying. `TagMappings`, flipbook groups, chain data,
  and PaperZD data are untouched, and Catalog/profile refresh remains read-only.

- **Character Catalogs now declare expected Animation Tags (TASK-161, 2026-08-01).** Each Catalog
  owns a base expectation set and each authored group carries additional expectations; one
  membership-aware accessor returns their deduplicated union for a saved character. The live Groups
  rail edits both sets through explicit undoable picker commits. One pure shared coverage resolver
  batches each Character Profile once, counts only exact own-authored Animation Tags as covered,
  reports group/chain carriers as near misses, and calls out coverage supplied only by a superset
  container. Refresh, load, and coverage recompute never seed tags or dirty either asset.

- **BREAKING: Animation Map Fragments are deleted (TASK-168/TASK-172, 2026-08-01).** The
  editor-only Fragment asset type, reusable slot/edge model, Profile instance data, factories,
  editor, validation, and APIs are removed outright. Any hypothetical serialized Fragment values
  are silently dropped on load/save with no CoreRedirect and no warning. Runtime and Blueprint
  consumers are unaffected because they were always Fragment-blind and the feature never shipped
  a user-reachable asset path. `feature/animation-map-fragments` is frozen archaeology: recovery
  means reverting the deletion commits, never merging that branch into the current codebase.

- **Cue placements now have a readable timeline identity (TASK-164, 2026-08-01).** Bars use an
  authored non-white Color first, then the project Tag Colors registry (exact tag before nearest
  ancestor), then the built-in convention for the replaceable `Paper2DPlus.Cue.Default` tag. Debug
  Name falls back to the Cue Type display name, label text contrasts with the fill, an instant Cue
  gains an anchor mark, and a Cue State uses an inset span so a one-frame state no longer looks like
  an instant. Existing behavior-error badges remain composed above the identity. Accessible summaries
  now carry the same label and timing form and append errors without dropping frame or duration detail.
  Fresh Cue Types receive the default tag on their generated class defaults only; native Cue CDOs,
  the Tag Colors settings registry, and Effect Profiles are not mutated.

- **Cue Type authoring now uses Unreal's My Blueprint panel for payload variables (TASK-162,
  2026-08-01).** The two bespoke payload/event list widgets are deleted. The restricted editor now has
  exactly four allowlisted tabs: My Blueprint, Details, Class Defaults, and Compiler Results. A compact
  Paper2DPlus **Override** control beside My Blueprint implements only the declared Cue events through
  the permitted event graph, including a wired parent call when a designer Cue Type extends another
  Cue Type; arbitrary function creation remains disabled. Quarantined legacy Cue Blueprints keep a
  warning/recovery body instead of exposing their forbidden graphs through My Blueprint. The editor
  also rolls back only timelines newly introduced during the session while preserving pre-existing
  legacy timelines for explicit recovery. Stock variable Details can display Replication settings;
  unsupported replication is still rejected and reverted by the persistence-boundary invariant.

- **BREAKING: Cue Types use ordinary designer-authored variables; the Paper2DPlus Effect-field kind is
  removed (TASK-167, 2026-08-01).** Effect Profiles remain fully supported as an independent visual
  library, including their editor, Catalog assignment, Blueprint queries, and
  `Paper2DPlus.Effect.Type.*` / `Paper2DPlus.Effect.Descriptor.*` classification tags. Removed from Cue
  Types: `Paper2DPlusEffectField` metadata, durable Effect-field declarations, Cue-side Type/Descriptor
  filters, the **Add Field > Paper2DPlus Effect** gesture and Details customization, Effect-picker focus
  preview, and the timeline's Effect-field thumbnail/ghost-frame path. The cue-to-Effect-Profile issue
  codes `Paper2DPlus.Character.FrameCue.EffectFieldOutOfLibrary` and
  `Paper2DPlus.Character.FrameCue.EffectFieldFilterMismatch` are also retired; once Cue behavior uses
  ordinary variables there is no truthful special “effect” property for those rules to inspect. A
  designer now adds the variables their Cue behavior needs directly. Every scalar soft Paper Flipbook
  variable still receives frame-zero warming through the cooked structural
  `CollectWarmableEffectArt` pass, with no marker or declaration. Cue Types that never used the retired
  field schema keep their durable fingerprint and remain Ready without restaging.

- **Panel polish: Effect preview focus rectangle, Effect Library width, Catalog button growth, and
  the Sprite Clipboard category (2026-07-31).** Four targeted fixes on top of the row unification.
  The **Effect Profile** preview drew its keyboard-focus rectangle whenever the panel held focus —
  and because clicking the preview focuses it (so keys reach the frame strip), scrubbing left a
  standing blue box around the whole surface. It now follows the platform focus-visible rule: mouse
  focus draws nothing, keyboard and programmatic focus still show the indicator, so the
  accessibility affordance and its test coverage are intact. The **Effect Library** column drops
  from 36% to 24% and the Preview grows from 38% to 50% (layout key `_v4_ProfileCompletion` →
  `_v5_NarrowLibrary`, since a saved v4 arrangement would otherwise restore the old split). In the
  **Character Catalog** details panel, relationship messages wrapped to two or three lines, which
  stretched the row and stretched the Open / Create and Assign / Remove buttons with it, leaving
  their labels stranded at the top; the messages now ellipsize with the full text in the tooltip
  (the policy the Character Profile path row already used) and every action button is
  `VAlign_Center`, so rows stay one line and buttons keep their natural height. The **Sprite**
  tool's separate **Clipboard** category is retired: Copy and Paste act on the same offset that
  section edits, so they sit under Reset Offset inside **Offset & Nudge**, leaving Sprite with three
  contextual categories instead of four.

- **Hand-rolled detail panels across the plugin now share one stock-Details architecture, and the
  Sprite and Root Motion onion controls became one shared "Onion & Skins" (2026-07-31).** The shared
  row helper (`FProfilePropertyRowUtils`) grew the pieces the remaining offenders needed:
  `MakeCustomRow` for widget labels, `MakeFactRow` for read-only derived data (rendered on the SAME
  column grid as authored rows, distinguished only by value color, and hiding its own grid line when
  the fact does not apply), `MakeSectionTitle`/`MakeSectionHint` for headings and help copy, and
  `MakeOnionChannelRow` for one onion channel. Converted surfaces: the **Character Catalog** details
  panel (its bespoke nested-`SBorder` row with a hard-coded 38/62 split is gone, so the Catalog now
  shares the plugin-wide drag-resizable column); the **Character Profile Animations** details pane,
  whose two `ToolPanel.DarkGroupBorder` "shadow boxes" are replaced by Timing and Connections section
  titles over flat grid rows, with the read-only facts, the hand-rolled Invul Frames row, the heading,
  and the empty state all moved onto shared rows and stock fonts; and the **Effect Profile** details
  panel, whose stacked bold-caption-above-control blocks became real label/value rows (Display Label,
  Effect Type, Descriptors) with the "optional" nuance moved into tooltips. **Onion & Skins:** the
  Sprite tool's side-by-side three-column mixer with vertical sliders is retired in favor of the
  vertical channel stack Root Motion already used, both tools now use that name, and both build it
  from the one shared channel row; the Reference channel keeps its Set / Go To / Clear actions as a
  following row. **BREAKING (already covered by this cut's MAJOR):** the public panel-id symbols
  `SSpriteEditorPanel::OnionReferencePanelId` and `SRootMotionEditor::PathSkinsPanelId` are renamed
  to `OnionSkinsPanelId`, and their id values change (`Paper2DPlus.Sprite.OnionReference` →
  `Paper2DPlus.Sprite.OnionSkins`, `Paper2DPlus.RootMotion.PathSkins` →
  `Paper2DPlus.RootMotion.OnionSkins`), which also resets those two Details categories to expanded
  on first open because their persisted expansion state is keyed by id. Note on text size: stock Unreal property rows use `PropertyWindow.NormalFont`, which is
  Regular 8pt — the same size these panels already used — so this pass fixes contrast, row metrics,
  and column alignment rather than raising the font above native.

- **BREAKING — the Character Catalog is now direct manipulation: no project scanning, no Scan/Apply, real
  authoring progress, and Groups moved into the Roster (2026-07-30, TASK-153).**
  The Catalog used to run continuous Asset Registry discovery over configured content roots and propose a
  diff you reviewed through **Scan Project** and committed through **Apply Changes**. That model made
  deletion adversarial: removal wrote a suppression record keyed to the exact asset path, so renaming or
  moving a profile resurrected it, deleting a row through Advanced re-added it on the next Apply, and
  Apply silently re-sorted the whole roster into path order. It is gone. Characters now enter through an
  explicit **+ Add Characters…** picker (filtered to Character Profiles, defaulting to a "not in this
  Catalog" lens, multi-select, one transaction, duplicates skipped) and leave through multi-select
  removal (card right-click, Delete, or **Remove Selected…**) that removes the entry and every group
  membership in one transaction and **stays removed**. Rows now project the saved roster in authored
  order; registry events only refresh row freshness.
  **Completion means something.** It used to mean "every *required* companion is assigned", and because
  the three requirement flags default to false, every character read as Complete having authored nothing.
  Character Profile, Character Layer, Effect Profile, and Combat Profile now export their manual
  completion checklists as hidden asset-registry tags, so a card shows real progress (`N/M authored`, with
  a meter) for characters whose assets are not even loaded, and the roster summary and completion filter
  follow. Because the criteria are manual ticks with no content-derived fallback and tags refresh on save,
  **"no checklist data" is an explicit state** — never silently rendered as complete. A resident asset is
  preferred over its tags so the Catalog cannot contradict an open Completion panel.
  **Groups became a rail inside the Roster** (layout `_v3_DetailsWarnings` → `_v4_GroupsRail`). The
  separate Groups tab is deleted: its "Add Selected Character" consumed the Roster tab's selection while
  sharing a tab stack with it, so the character being added was invisible. The rail lists **All
  Characters** plus each group with member counts; click to scope the grid, drag cards onto a group to add
  them, drag within a scoped group to reorder members, drag a rail row to reorder groups, rename labels
  inline, and create/delete from the context menu (deletion is confirmed and keeps every character).
  Creating a group now takes one typed label and derives the stable internal Blueprint name itself, and
  rename touches the label only, so `GetEntriesInGroup` callers cannot be broken by a rename. A scoped
  grid is now sorted into authored member order — it previously showed Catalog order, which meant
  reordering would have rearranged data the designer could not see.
  **Companion assignment is manual plus an explicit suggestion.** Automatic link-mode reconciliation is
  gone; Details gains **Suggest Companions**, which fills only *empty* Layer/Combat slots whose inward
  Character relationship resolves to exactly one asset, in one undoable transaction, and reports
  ambiguous / unmatched / legacy-uncertain slots instead of guessing. Effect is never suggested — it has
  no inward relationship tag.
  Removed public symbols and surfaces: `FCharacterCatalogDiscoveryService` (whole header),
  `EPaper2DPlusCharacterCatalogDiffKind`, `EPaper2DPlusCharacterCatalogSyncResult`,
  `FPaper2DPlusCharacterCatalogProjection` / `…ProjectionRow` / `…Revision`,
  `EPaper2DPlusCatalogLinkMode`, `FPaper2DPlusCharacterCatalogOrphan`,
  `FPaper2DPlusCharacterCatalogOrphanGroupMembership`, the Catalog asset's `OrphanedEntries` and the three
  per-entry `*LinkMode` fields plus `bDeprecatedEffectLinkSeedInspected`,
  `UPaper2DPlusSettings::CharacterCatalogContentRoots`, the `SCharacterCatalogGroupsPanel` widget, the
  Catalog editor's Groups tab, `FProfileRelationshipService::Resolve` (split into `ResolveAssigned` and
  `SuggestCandidate`), the `Paper2DPlus.Catalog.SyncConfiguredAndSave` console command, and
  `scripts/sync-paper2dplus-character-catalog.ps1`. Retired validation codes:
  `Settings.InvalidContentRoot`, `Settings.MissingContentRoot`, `Discovery.StaleProjection`,
  `Discovery.UnsynchronizedSnapshot`, and the `Relationship.{Layer,Combat}.{Ambiguous,LegacyUnknown}`
  family; new code `Paper2DPlus.Catalog.Entry.MissingCharacterAsset`. Group issues now navigate to the
  Roster tab discriminated by `ToolId = "CharacterCatalogGroups"`.
  `UPaper2DPlusSettings::DefaultCharacterCatalog`, the runtime group model, and every Blueprint Catalog
  query (`GetCatalogEntries`, `FindEntryByCharacterProfile`, `GetCatalogGroupNames`, `GetEntriesInGroup`,
  `FPaper2DPlusCharacterCatalogEntry`) are unchanged, so game Blueprints need no edits.

- **The Combat Profile editor now follows the Character Profile workspace grammar (2026-07-29, TASK-152).**
  Layout `_v7` → `_v9_CharacterGrammar`. The two editors are now the same shape: central designer
  tools — **Overview** (formerly Setup), **Score Playground**, and **Combat Lab** — sit in one stack
  and each carries the SAME shared current-item header the Character editor uses for animations
  (`SAnimationProfileSwitcher`, here bound to the combat attack picker, so art preview, search, and
  focused Up/Down adjacency all behave identically). The right column is the familiar vertical pair:
  contextual **Details** on top, which shows the selected attack's tuning and follows that selection
  no matter which tool is in front, and **Completion** / **Related Profiles** as a sibling stack
  below — they previously consumed the lower-LEFT area, unlike every other profile editor.
  Variables, Tag Defaults, Scoring Profiles, Scenario Presets, and Advanced Details are closed tool
  tabs. Because per-attack tuning moved into that shared Details panel, you can now edit a scoring
  rule and watch the move's rank change in Score Playground without switching surfaces.

  The editor used to render the same derived attack roster four ways — the Setup card grid, an
  `Attacks` dropdown picker, the Attack Catalog tab's pinned list, and an 11-column facts table that
  truncated every interesting cell at its docked width — kept in step by a selection synchronizer.
  The Attack Catalog tab is retired: the Overview card grid is
  the single selectable surface and gains a search box over move names and attack tags, and the
  derived facts fold into card badges (reach; a ● only on moves actually customized here — the
  constant "w 1.00" noise is gone) and a read-only facts line in the inspector. The inspector itself
  is rebuilt as guided sections: identity/facts header (art, tag, reach / active frames / damage /
  root motion in plain words) with a live rank + score badge for the current Score Playground
  situation, the layered effective-scoring-rules view (all / tag / move provenance), a
  "+ Scoring Rule" template menu, bespoke Weighting / Range / Eligibility rows, a property grid
  scoped to this move's rules / tags / variable overrides, and a "Revert to inherited defaults"
  action. Move Name / Move Flipbook no longer appear as editable rows (editing them silently
  unlinked the tuning row). The score badge, template menu, and effective-rules view had shipped as
  dead code behind a never-mounted widget host — they are now live surfaces. Copy pass throughout:
  one vocabulary (attacks come from the Character Profile, a move may be *customized here*, scoring
  only advises), and "inherited" no longer means two different things on one screen. The shared
  scoring-variables strip collapses into an expander while empty. The visual tour's combat
  first-run screen asserts the single-surface Setup guidance instead of the retired catalog pairing.

  Score Playground finished the same pass. Its scoring-profile / scenario-preset strip was the last
  hand-laid-out row in the workspace; those controls and their **Load** / **Save current** actions
  now sit in the left column under a **Scenario** heading, above **Situation**, and both groups build
  on the shared `FProfilePropertyRowUtils` rows — so they align with each other and with the real
  property grid in the collapsed area beneath them, and the Combat editor stops being the one
  profile editor with its own row idiom. The three situation numbers no longer stretch across the
  column or render a meaningless decimal (`150.0` → `150`; both fractional-digit bounds have to be
  set, because the numeric interface takes `Max(Max, Min)` and `Min` defaults to 1). The ranked list
  now takes all the height it can and the score explanation sizes to its terms, replacing the fixed
  45/55 split that left a half-empty bordered box at tall window sizes. Profile Completion — shared
  by Layer, Effect, and Combat — states that its criteria are ticked by hand, so a fully authored
  profile reading `0 / 5` no longer looks like a broken meter.

- **Hitbox tool chrome is now one mode-aware toolbar (2026-07-29).** The Hitbox tool's left-hand
  vertical "Tools" column is gone; **Hitboxes** and **Socket** sit side by side at the head of the
  top toolbar, and the rest of the bar swaps with the selected tool. Hitbox mode keeps its existing
  controls (Hitbox Overlays, Show ATK/HRT filters, Draw Attack/Hurtbox), which previously stayed
  visible — and misleadingly interactive — while placing sockets. Socket mode now has its own
  controls: an **Add Socket** button (center-of-sprite placement, same as the sidebar action) and a
  new **Socket Overlays** toggle that paints gold socket markers over the frame-strip thumbnails,
  mirroring what Hitbox Overlays does for silhouettes. Keyboard shortcuts are unchanged (E/Q tool
  switch, 1/2 draw type). The canvas, sidebar sections, and layer-scoped provider behavior are
  untouched.

- **Hand-rolled detail panels now share one stock-Details row architecture (2026-07-29).** New
  `FProfilePropertyRowUtils` (`ProfilePropertyRow.h/.cpp`) reproduces the anatomy real Details
  panels get for free: every row is its own splitter bound to ONE shared column state (the
  engine's `SDetailSingleItemRow` + `FDetailColumnSizeData` recipe), so the 1px vertical divider
  lines up into a real column across rows and dragging it on any row resizes the whole column
  together; each row draws a 1px bottom grid line plus a hover tint (the "grid rowing"); labels
  use the engine's `PropertyWindow.NormalFont` instead of ad-hoc 8pt fonts; and value widgets
  fill only the value column instead of a dropdown holding "None" stretching across the whole
  dock. Converted surfaces: the Hitbox tool's Properties (Defense/Type/Pos/Size/Z/Depth/Damage/
  Knockback/Clash, socket Name/Pos, Apply-to) and Frame Operations sections, Frame Timing's
  Batch Duration Tools, Sprite's Batch Offset Tools, and Root Motion's Batch Position Tools —
  including one shared string-combo factory replacing five per-panel copies. Redundant inner
  section titles that duplicated the hosting category header ("Properties", "Frame Operations",
  "Hitboxes & Sockets") are gone. New panels get the native look from one `MakeRow` call instead
  of hand-rolling `AutoWidth label + FillWidth value` boxes.
- **The Combat attack inspector joins that shared row grid (2026-08-04).** Its Weighting, Range, and
  Eligibility rows were hand-rolled `FillWidth(0.45)/FillWidth(0.55)` boxes, so the workspace built
  to mirror Character/Catalog/Effect visibly did not match them — no draggable column divider, grid
  line, hover tint, or `PropertyWindow.NormalFont`. They now go through `FProfilePropertyRowUtils`,
  and its section headers use the shared title/hint pair.
- **One shared bounded thumbnail budget (2026-08-04).** `TProfileThumbnailBudget`
  (`Public/ProfileThumbnailBudget.h`) is now THE least-recently-used retention cap for virtualized
  card art. The Catalog Roster and the Combat attack browser each grew their own in the same cut,
  with different containers and different eviction cost — two copies to fix for any cancellation,
  keep-alive, or budget bug. Each panel keeps its own load graph (the Roster's two-stage Profile →
  flipbook resolve, the browser's single flipbook) and a release hook still cancels in-flight loads
  on eviction; only the LRU is shared. The browser's eviction drops from a full scan to O(1).
- **Catalog filtering stopped allocating per row per group (2026-08-04).** `PassesFilters` asked
  "is this row in this group" for every row against every group, and each answer walked the group's
  members allocating two strings per comparison — tens of thousands of allocations per keystroke on a
  few-hundred-character Catalog. `RefilterRows` now builds one character → group-labels index up
  front.

### Data Model / Deprecations

- **`FAlignmentMetadata::GroundPlaneOffset` is formally deprecated (2026-08-04).** Padding places
  every cell midpoint-to-midpoint — the same anchor the uniform trim bakes as the sprite pivot, so
  trim and pad agree by construction — and `PadTextureInPlace` has ignored this value (and logged a
  non-zero one) since that change. The field carried no deprecation marker, so it still read as live
  authored data. It is retained rather than drained: assets padded by earlier releases hold the only
  surviving record of their layout there, and clearing it on load would destroy that on the next save
  for no benefit. No behaviour change and no migration.

### Runtime

- **The Character Profile can now place the sprite at runtime, opt-in (TASK-156, 2026-08-04).**
  `UPaper2DPlusCharacterProfileComponent::bApplyProfileRelativeTransform` (default **false**) applies
  the profile's authored Relative Transform to the resolved flipbook component at BeginPlay and on
  late profile or flipbook assignment, through one funnel. `GetRelativeTransform()` was previously
  read by nothing in the plugin, so sizing was necessarily done in a Blueprint and transcribed back
  by hand. Default-off is deliberate: every project already applying that value in its own spawn
  code would otherwise place the sprite twice, and a silent doubled offset is expensive to trace.
  The apply is an **absolute assignment**, not a retained delta, so it is idempotent and cannot
  compound with a game-side apply of the same values; turning the option off restores the
  component's own authored transform. When it changes scale it also retires and re-seeds the
  per-frame sprite-offset record, which is stored in world units measured against the previous
  scale. Note the authority caveat documented on the property: when the flipbook component IS the
  actor root, moving it can fight replicated movement.
- **`Paper2DPlusLayerDraw::PixelSizeToWorld` is new.** A magnitude sibling to `PixelOffsetToWorld`,
  which is correct for positions and wrong for sizes: it negates X when facing left and computes Z
  as `-OffsetPx.Y`, so a pixel height converted through it comes back negative and a left-facing
  preview mirrors a measured width.

### Fixes

- **Frame Cue dispatch no longer asserts when a ledgered cue is caught mid-garbage-collection
  (2026-08-09).** Range ledgers and composed Layer views are deliberately GC-invisible, so a cue
  object the garbage collector's reachability pass has already marked can still be sitting in an
  active set when a force-end or transition dispatch runs. Such an object passes `IsValid` (it is
  not pending-kill yet) but `ProcessEvent` on it asserts (`!IsUnreachable`), which took down whole
  automation suites on UE 5.0–5.4, where that GC pass runs between tests. Liveness now includes
  reachability at every funnel: `IsPlacementResolvable` treats an unreachable cue as unresolvable,
  and the force-end teardown and stale sweep drop unreachable ledger entries without dispatching —
  nothing can safely receive a dying object's End anyway. Pinned by
  `Paper2DPlus.FrameCues.Dispatch.UnreachableCueIsNeverDispatched`. Caught by the v8.0.0 release
  gates; no released version carries the crash.
- **Opening a profile editor no longer asserts on UE 5.0–5.4 hosts whose module never saw the
  play-world command registration (2026-08-09).** The shared profile toolbar's PIE group called
  `FPlayWorldCommands::Get()` directly, but on UE 5.0–5.4 that template's backing instance is
  per-module — this plugin's copy stays unset even though the level editor registered the command
  set long ago, so `Get()` dereferences an invalid instance (`Assertion failed: IsValid()
  [SharedPointer.h]`), and `Register()` cannot be the fix there because its instantiation needs
  the private, unexported `FPlayWorldCommands` constructor and does not link. `Install` now
  resolves the engine-side registration through the process-wide `FInputBindingManager`
  ("PlayWorld" context, `RepeatLastPlay` command) and requires the bound global play-world action
  list — proof the engine's own instance is live — before appending the actions and calling
  `BuildToolbar`, which executes inside UnrealEd against that instance. A minimal host without
  the play-world stack skips the PIE group instead of asserting. Caught by the v8.0.0 release
  gates before shipping; no released version carries the crash.
- **Two stale visual-tour checks corrected (2026-08-10).** The Effect workspace probe still
  expected the pre-v5 36/38/26 column split and rejected the shipped
  `_Layout_v5_NarrowLibrary` 24/50/26 geometry on all three Effect screens; it now mirrors the
  authored layout coefficients. The narrow Animations-List check demanded the full "+ Add
  Flipbooks…" label, which the compact browse bar legitimately elides to "+ Add…"; it now accepts
  both presentations, the same rule the Organize control already had. Both were checker defects —
  the captured screens show correct product surfaces.
- **The Frame Cues preview keeps post-processing enabled on UE 5.0–5.3 (2026-08-10).** The preview
  viewport's designer-visuals flags were set before `SetViewportType(LVT_OrthoXZ)`, and on UE
  5.0–5.3 the engine's `ApplyViewMode` force-clears `PostProcessing` for every orthographic view
  when a view type or mode is applied (the clamp is gone in 5.4+) — so a designer's post-process
  cue (a screen flash) previewed invisibly there. The flag block now runs after the view-mode
  calls; the flag is an ordinary user-toggleable ortho state on those engines, so the preview
  contract — post-process output from designer Cues renders in the preview — now holds across the
  whole supported range. The status-text test also names exactly which visual flags are disabled
  when this ever regresses. Caught by the v8.0.0 release gates; no released version carries it.
- **Three automation tests are now engine-honest across UE 5.0–5.8 (2026-08-09).** The two
  behavior-envelope tests expected the refusal diagnostic in the compiler log on every engine, but
  only UE 5.6+ compiles run full-Blueprint data validation
  (`BP.bDoFullDataValidationDuringCompilation`) — older engines validate only the CDO, so the
  message cannot exist there and the tests failed falsely on 5.0–5.5; the expectation is now
  declared only where that CVar routes the message into the log, while the substantive envelope
  assertions stay unconditional everywhere. The failed-Save-All recovery test's expected error
  pattern included the `LogFileManager:` category prefix, which UE 5.0–5.5 do not include in the
  captured message; the pattern now matches the bare `Error moving file` text both shapes share.
- **The two preview-audio automation tests now skip honestly on audio-disabled hosts
  (2026-08-09).** `Preview.Behavior.ContextAwareSoundUsesPreviewPath` and
  `Preview.WorldOwnedAudioIsHostIsolated` assert on real world-owned audio components, which the
  engine refuses to create while all audio is disabled (`-nosound`, audio-less CI). They failed as
  false negatives on such hosts; both now gate those assertions on `GEngine->UseSound()` and
  record an explicit skip, while every audio-independent assertion (preview-sound request routing,
  parameters) stays asserted everywhere.
- **A sideways sprite offset no longer walks the character away from its capsule on facing flips
  (2026-08-05).** The per-frame sprite offset (`SpriteOffset + TrimOffset`, the Sprite tool's Offset &
  Nudge) was retained as a WORLD-space record and applied as `AddWorldOffset` deltas. A facing flip
  that rotates the actor/capsule — controller yaw, the standard side-scroller setup — mirrors the
  already-applied offset in world space all by itself, so the record went stale on every flip and the
  next key-frame commit double-applied the correction: each left/right flick pushed the sprite a
  further 2x the authored offset outward (the wallslide repro), and `RetireAppliedSpriteOffset` left
  the same residue behind on every animation change. The record is now the PARENT-SPACE delta
  actually added to the component's relative location (`LastAppliedSpriteOffsetLocal`), applied and
  undone via `AddRelativeLocation`, which a parent-basis flip cannot invalidate — after an actor-yaw
  flip the fresh commit resolves to the identical parent-space value and correctly no-ops, while
  sprite-self scale/yaw flips still mirror through the commit. Zero-offset animations are
  byte-identical. Regression coverage: `Paper2DPlus.Facing.SpriteOffset.*` (yaw-flip no-drift,
  self-scale mirror, retire-after-flip residue).
- **Catalog Roster drag-reorder no longer rewrites authored order it was not aiming at
  (2026-08-04).** Both reorder gestures translated a VIEW index into an authored array index. The
  card-on-card member reorder read an index out of the filtered visible rows, but `RefilterRows`
  filters before it stable-sorts survivors into member order, so any active search or completion
  filter made the drop land on a different member than the indicator showed. The Groups rail reorder
  mapped rail row N to authored group N-1, but the rail omits unnamed groups, so every unnamed row
  ahead of the drop shifted the landing slot. Both now pass the ANCHOR the designer dropped onto
  (`FCharacterCatalogEditorModel::MoveGroupMemberRelativeTo` / `MoveGroupRelativeTo`) and the model
  resolves its authored index itself, which makes the whole mistake unrepresentable rather than
  guarded. Dropping below the All Characters sentinel now explicitly means "make this the first
  group". `Groups[].Members` order is read by runtime Blueprint Catalog queries, so this was silent
  corruption of shipped data, recoverable only by one undo before later edits buried it.
- **Renaming an animation no longer breaks Profile-Socket Frame Cue anchoring for the rest of the
  session (2026-08-04).** `RenameFlipbookAndPropagate` invalidated only the lowercase name cache, but
  the exact-name index (`ExactAnimationNameToDataIndicesCache`) is owned by the OTHER cache, and a
  rename leaves the element count unchanged so nothing else could notice. `FindExactFlipbookData` was
  the one lookup with no linear-scan repair, so it returned null permanently and
  `ResolveFrameCueAnchor` answered `ProfileRowNotFound` — every Profile-Socket-anchored cue on that
  animation silently lost its socket transform in PIE and in preview. Both caches are now invalidated
  together, and that lookup gained the same repair fallback its two siblings already had.
- **Authoring a Frame Cue during PIE now raises the detection watch rate (2026-08-04).** The
  "does this profile/Layer carry any cue" scans were memoized on asset identity alone, which cannot
  see a cue added to an asset a live actor is already pointing at — so the component kept the 20 Hz
  poll and a short animation's newly authored cues never fired, reinstating exactly the silent miss
  the every-frame rate exists to prevent. Both memos now also key on an editor-only content revision
  (`GetEditorContentRevision`, bumped from `Modify`/`PostEditUndo`/`PostEditChangeProperty`). Cooked
  builds report 0 always, so runtime behaviour and cost are unchanged.
- **A Character Catalog edit no longer rebuilds every panel of an open Character Profile editor
  (2026-08-04).** The Expected Tags panel installed the project Catalog into the shared model's single
  `SecondaryWatchedObject` slot — the Layer workspace's dual-asset channel, which drives
  `OnAssetExternallyModified` and therefore a workspace-wide rebuild. Because the tab is open by
  default, every Catalog transaction tore down in-progress inline renames and reset list scroll in an
  editor the designer was not even looking at. The panel now watches the Catalog through its own
  scoped subscription and refreshes only itself.
- **An unfinished companion assignment is a Warning again, not a gate failure (2026-08-04).** Layer
  and Combat assets always publish the inward relationship tag, writing the literal `"None"` when
  their own link is unset, so a companion assigned before its link was filled in resolved as
  `ManualMismatch` — a hard Error reading "points to a different Character Profile" about an asset
  that points at nothing, which failed `validate-paper2dplus` on a clean project. That state is now
  the distinct `ManualUnlinked` Warning. The same pass restored two checks the link-mode removal had
  dropped entirely: `Relationship.*.LegacyUnknown` (Warning) and, on an EMPTY slot with several
  inward claimants, `Relationship.*.Ambiguous` (Error when the slot is required, else Warning).
- **A Frame Cue preview error badge clears when the placement runs clean again (2026-08-04).** The
  badge map had no production clear path — the only erasers were a visual-tour fixture helper and a
  test-only reset — so a badge raised by a payload value the designer then fixed advertised a stale
  failure until the editor restarted, and entries for garbage-collected placements accumulated for
  the whole session. A clean re-dispatch now retires the badge and prunes dead entries; the
  log-once-per-placement behaviour is unchanged.
- **White is an authorable Frame Cue bar colour (2026-08-04).** The timeline treated "Color differs
  from White" as "the designer authored a colour", but `Color` DEFAULTS to White — so white was the
  one colour that could not be chosen, and near-white was swallowed by `FLinearColor::Equals`
  tolerance. Authorship is now an explicit editor-only `bOverrideColor` flag on
  `UPaper2DPlusCueBase`. It is deliberately editor-only: `Color` has no runtime reader, and the
  durable Cue Type schema hashes every persistent inherited property, so a serialized flag would have
  changed every saved Cue Type's fingerprint and forced a restage. Placements left at the White
  default keep resolving from the Tag Colors registry exactly as before.
- **Game code that clears `OnFinishedPlaying` no longer costs Cue States their `Completed` end
  reason (2026-08-04).** `UPaper2DPlusFlipbookComponent` latched natural completion by binding the
  ENGINE's BlueprintAssignable finish delegate in its constructor. Project code manages that delegate
  routinely (the stock unbind node, a `Clear()` when re-arming a one-shot), and removing the
  project's handler removed the plugin's with it, so a natural finish was reported as an ordinary
  stop and a cue on the final frame could be skipped. The binding moved to `OnRegister` (so it is no
  longer serialized into Blueprint component templates, where designers saw an event they never
  added), is re-asserted before each `Super::TickComponent`, and is backed by a pre-Super-snapshot
  fallback mirroring the engine's own terminal arithmetic including reverse playback.
- **A nested preview adapter dispatch no longer orphans the outer adapter's resources
  (2026-08-04).** `EndAdapterDispatch` reset the active owner token to `INDEX_NONE` instead of
  restoring the enclosing one, so after a nested dispatch — which adapter code can trigger
  synchronously — everything the OUTER adapter allocated was unowned, survived its own
  `ReleaseResourcesForOwner` sweep, and kept sounds playing and proxies drawing past its teardown.
  The bracket is now a save/restore pair with an RAII `FScopedAdapterDispatch` guard.
- **The first Space press starts playback in the Frame Cues, Frame Timing, and Root Motion tools
  without clicking a frame first (2026-08-03).** Activating a tool now seats keyboard focus through
  one shared deferred seat (`FProfilePanelFocusSeat`) that runs after the activation callback
  returns — the previous synchronous seat re-entered dock-tab activation mid-focus-commit and was
  aborted by Slate's interrupt checks, so focus never actually landed and Space died at the window.
  The seat also re-seats when a stale focused descendant would swallow Space (a button, a search
  box), while leaving genuine text editing and Space-forwarding widgets (the curve editor, the cue
  timeline track) undisturbed. Frame Timing and Root Motion previously had no seat at all.
- **Selecting a cue track no longer rebuilds the timeline's header column (2026-08-03).** The
  active-track visuals are live attribute bindings now, so a header click repaints instead of
  destroying and rebuilding the very button whose click handler is still on the stack; structural
  track changes (add/remove/rename) still rebuild.
- **A manually driven playback source can no longer be permanently silenced by one observed stop
  (2026-08-03).** PaperZD — and any driver that calls `Stop()` once and then positions the sprite by
  hand — never flips `IsPlaying` back to true, so the observed playing→stopped terminal claim was
  never followed by the stopped→playing observation that reopens dispatch: every later cue on the
  component was dropped silently, while the Frame Cues editor preview (which bypasses detection)
  kept firing. Observed frame movement on the bound source now reopens a native dispatch generation
  automatically, so PaperZD-driven characters dispatch with zero integration code; the explicit
  `BeginExternalFrameCuePlayback`/report seam remains the precision option for exact End reasons.
  Pinned by `Detection.ManualDriveReopensAfterObservedStop` on both the custom and stock component
  paths.
- **A dead Character Layer asset can no longer put freed cue pointers into terminal teardown
  (2026-08-03).** The composed Layer-cue view is deliberately invisible to the GC and safe only
  behind a live appearance digest; the terminal claim and the test-only force-end seam snapshotted
  it without the dead-asset guard every other reader applies, so a Layer asset collected in the
  window between digest teardown and the next frame change could be dereferenced — or scanned by
  the GC from the strong terminal-order array — during EndPlay or an animation boundary. Both sites
  now skip the orphaned view; its active ranges are still dropped by pointer identity, never
  dereferenced.
- **`GetExpectedAnimationTagsForCharacter` now applies the same group-validity rule as
  `GetEntriesInGroup` (2026-08-03).** An unnamed group, or a later group whose name duplicates an
  earlier one, is unaddressable by every query surface — but it still contributed its expected
  Animation Tags to the membership union, so the coverage checklist could demand tags no reachable
  surface could explain. Both paths now share one addressable-group predicate; editor-authored
  groups (always uniquely named) are unaffected.
- **Bulk extractor auto-pad no longer deletes each animation's vertical motion (2026-07-31).**
  `FSpriteExtractionUtils::PadTextureInPlace` scanned **every cell** for its own content bottom and
  snapped that bottom to one ground line. Work the algebra through and the destination content bottom
  collapses to `DstCellH - GroundPlaneOffset - 1` — a constant, for every frame, regardless of where the
  artist drew it. Padding a batch therefore welded each animation flat: measured against the shipped art,
  `Aerial_DownAttack`'s 26px drop became 0 and `Roll`'s 9px rise became 0, and 176 of 288 character sheets
  were warped, the worst by 93px. Grounded animations looked fine, which is why it went unnoticed — their
  vertical motion is already zero, so flattening changed nothing.
  Placement is now **midpoint-to-midpoint**: `CellOffsetY = DstCellH / 2 - SrcCellH / 2`, mirroring the X
  axis, which was always correct. Every cell of a sheet shifts by the same amount, so relative frame
  positions survive exactly. Measured across two independent character libraries (462 sheets), the feet
  sit a constant 31px below the canvas centre at every cell height while the distance from the cell
  *bottom* varies (0 vs 32) — the art is centred, not bottom-anchored. The **cell midpoint is the one
  anchor**, and it is the same point the uniform trim already bakes as the sprite pivot
  (`TrimMaxExtLeft, TrimMaxExtTop`), so trim and pad now agree by construction rather than by luck:
  trimmed-then-midpoint-padded is identical to untrimmed-then-centred on all 445 character sheets, while
  anchoring on a *trimmed* cell's bounding-box centre diverges on 96% of them by up to 16px. That
  invariant is pinned by `PadTextureInPlace.TrimAndPadAgree`, which fails if box-centre anchoring is ever
  reintroduced. `GroundPlaneOffset` is retained as an ignored parameter for source compatibility and the
  clip-failure branch is deleted — a centred cell provably cannot overflow its destination.
  `ApplyCrossSheetAlignment` now derives each expanded region from its destination-cell origin instead of
  an anchor enum, which fixes an off-by-one on odd cell sizes and keeps tight (trimmed) source rects at
  their real in-cell height. Single-row sheets are byte-identical; **multi-row sheets change**.

- **The single-sheet extractor can auto-detect its grid (2026-07-31).**
  Grid mode always required you to type Columns and Rows and guess. **Auto-detect Grid** now reads
  them off the pixels with the same `FSpriteExtractionUtils::DetectFrameGrid` the bulk extractor
  uses, so a sheet that shatters island detection into dozens of blobs — a potion VFX yields 65
  islands for 11 frames — resolves correctly, and no `IslandMergeDistance` tuning is involved. It
  **fails closed**: a low-confidence or single-frame answer leaves your Columns/Rows untouched and
  explains why rather than silently overwriting a grid you tuned by hand. It reports the deciding
  rule, and names trailing blank frames as deliberate VFX length-padding so a padded sheet does not
  read as a detection error. Island detection stays the default and keeps its parameters.
  Two deliberate non-changes: this window never pads, so the midpoint pad anchor has no counterpart
  here; and its uniform-bounds anchor stays a designer-facing dropdown (BottomCenter default) rather
  than adopting the pad's midpoint rule — there the anchor was a hidden algorithm decision, here it
  is a control.

- **Frame detection survives TRIMMED art (2026-07-31).**
  The gutter rule was all-or-nothing: one stray pixel on one boundary disqualified the true grid and
  collapsed detection to a far coarser one. A trimmed 2816×128 explosion sheet read as **2 frames
  instead of 16** because a single boundary column held a single pixel, while fourteen of its fifteen
  boundaries were perfectly empty. Boundaries may now hold a few bleed pixels, but only on strong
  evidence — `≤ max(2px, 3% of column height)`, **and** at least 4 boundaries, **and** at least 75% of
  them exactly empty. Both clauses are load-bearing: tolerance alone split single-frame static poses
  whose art merely has thin vertical gaps (measured, genuine splits run 10–15 boundaries at 80–93%
  perfectly clean; the false positives had 2 at 50%). The bleed tier is consulted **only when no clean
  grid exists**, so the change is provably additive — applying it as a peer of the clean rule
  regressed 11 sheets by clipping sword tips and thin VFX trails. A **twin rule** now also lets
  identical-dimension sheets in one folder share a grid, which rescues a bleed-damaged sheet sitting
  beside an obvious sibling where folder consensus stays silent (that folder held six sheets in five
  formats, so it had no dominant format at all). Validated across three shipped libraries — 462
  sheets, **0 sliced, 108/109 agreement with the artists' own folder labels**. Full derivation,
  including the five approaches that failed first, in
  `docs/solutions/ue-sprite-sheet-frame-grid-detection-patterns.md`.

- **De-bake is its own window and stops hijacking the extractor's canvas (2026-07-31).**
  De-bake groups lived in the extractor's right panel and pushed their preview into the **main**
  center canvas, so selecting a group silently replaced the texture the rest of the window was
  reasoning about — grid overlay, cell size and detection readout all switched to a preview image
  while the list still showed the real selection. De-bake now opens its own window (Advanced ▸
  De-bake) with its own preview canvas and frame strip, and closing it clears the preview state. The
  extractor's canvas shows the selected texture and nothing else, permanently.

- **Bulk extractor bottom bar collapses into an Advanced menu (2026-07-31).**
  Export/Import Organization, De-bake, Manage De-bake Groups and PaperZD Sequences moved behind one
  **Advanced** menu, leaving Batch Rename, Organize Folders, Auto-Pad and Extract All as the everyday
  bar. Each entry explains *why* it is unavailable — "Select two or more ungrouped variant sheets
  first", "Link a Character Profile first" — instead of greying out silently. The menu also gains
  **Add Character to Catalog**, which registers the linked profile in the project's Character Catalog
  using the same one-transaction append the Catalog editor's own intake uses; extraction is where a
  character first exists, so it is the natural moment to register it.

- **Trim no longer defaults on (2026-07-31).** Trimming packs every sprite to its shared uniform
  bounds and bakes the cell-midpoint pivot. That is correct, but it is a one-way transformation of
  the extracted output and a designer who did not ask for it should get plain full-cell sprites.

- **Bulk extractor frame detection reads the pixels instead of guessing (2026-07-31).**
  Grid inference fell back to "assume square cells", which sliced through artwork on 99 of 297 sheets in
  one library — `Aerial_DownAttack` (2304×128) was read as 18 frames of 128 when the real pitch is 192,
  cutting every frame in half. New additive `FSpriteExtractionUtils::DetectFrameGrid` decides from a
  **gutter rule**: a candidate cell is valid only when every internal boundary column is empty and blank
  cells appear solely as a capped trailing run, searched most-frames-first. Trailing blanks are preserved
  because VFX sheets are padded to match the animation they accompany — rejecting them forced a coarser
  grid that silently *merged* real frames (a 1280×96 sheet read as 5 frames of 256 instead of 10 of 128).
  Interior blanks still disqualify a grid, and the cap stops a lone sprite on an empty sheet
  over-fragmenting. A filename size hint wins when the pixels do not contradict it; folder consensus can
  break a tie but only within the same frame format, so a 64-tall VFX strip never inherits a 128-tall
  character grid; rows split only on an explicit hint, because a transparent band through a character is
  not a row break. Across 462 sheets in two libraries: **0 sliced, 108/109 agreement with the artist's own
  folder labels.** Island detection and `IslandMergeDistance` are untouched and still available.

- **Bulk extractor: 17 organizer, list and workflow fixes (2026-07-31).**
  Folders no longer unfold after adding an item (collapse state is keyed by stable node identity rather
  than a path string that changes under reparenting, which also fixes descendants unfolding on rename and
  an empty folder unfolding the moment it gains a child). Folder names are validated **while you type**
  against the real package-name rules, and the import route fails closed instead of "sanitizing" a name
  into a corrupt path. Renaming a texture now re-sorts and re-searches it immediately (both keyed off
  `DisplayName` rather than the underlying texture name, so `Knight_Idle` renamed to `Idle` no longer
  sits among the K's), and rows can be renamed inline in the main list. Arrow keys change the selected
  texture from anywhere in the window instead of being captured by whichever spinbox has focus, follow
  the active search filter, and leave modified arrows to the list's own range-selection. Deleting a
  texture keeps a neighbouring row selected instead of jumping to the first. **Extract All now explains
  why it is disabled** — naming the blocking count and the differing cell sizes — and the status chips
  are legible and filterable, so unconfirmed rows are findable in a 200+ batch. New **Export / Import
  Organization** round-trips the whole folder tree, per-texture names and naming settings to JSON; import
  stages matches first and reports unmatched or illegal rows rather than silently dropping them. The
  Organize Folders tree and Batch Rename both gained a search box, folders gained tooltips and
  multi-select move, a new folder takes focus and scrolls into view, Delete deletes a folder, and the
  PaperZD sequence pre-check now shares one predicate with the extract path so the two cannot disagree.
  The redundant **Create subfolders** checkbox is gone from this window: folder assignments always took
  precedence over it, `GetPath()` already appends a per-item leaf folder, and the organizer's Apply
  silently unchecked it anyway. An unassigned texture now always lands in its own folder. The
  single-sheet extractor keeps its own separate checkbox, where it is the only output-folder control.

- **Details-tab edits and undo no longer reset the Score Playground situation (2026-07-29, TASK-152).**
  The Combat Profile toolkit kept its own never-edited copy of the preview context and
  scoring-profile choice and re-applied both to the shared editor session on every derived-view
  refresh — so any Details (Advanced) edit, Generate, or undo snapped the playground back to its
  defaults (distance 100, both fighters at full health, the Default profile). The session now
  solely owns the preview context and scoring-profile selection.

- **Flipbook Draw edits re-derive the frame's bounds instead of leaving it cropped and offset (2026-07-27).** A Paper2D sprite's render and collision geometry can be *derived from its source pixels* — `TightBoundingBox` (the engine's default for every sprite's collision geometry), `ShrinkWrapped` and `Diced` all scan the source alpha — and the result is then serialized onto the sprite. Nothing invalidates that cached geometry when the texture's pixels change: `UPaperSprite::PostLoad` only rebuilds on asset *version* upgrades. So any **Edit Frames (Paper2D+ Draw)** edit that moved or resized the art — a nudge, a flip, a rotate, an erase along the silhouette — left that frame drawing and colliding against the box that fitted the art *before* the edit. The visible symptom is a frame that renders noticeably thinner and shifted with part of the character sliced off, and it survives reopening the asset, because the stale bounds are on disk. It also appeared to fix itself if you changed a sprite property and changed it straight back — the *edit* fired `PostEditChangeProperty` → `RebuildData`, so the value being restored was irrelevant. Committed pixel writes now rebuild the derived geometry and dirty the **sprite** package (the bounds live on the sprite, not on the texture that the write already dirtied, so the correction has to be saved alongside it). Sprites using `SourceBoundingBox` or fully custom geometry were never affected — that geometry does not read pixels. Frames already damaged are repaired by committing any further edit to them in the draw tool, or by changing any property on the affected sprite and saving.




<!-- Frame Cues rework (notify-style Cues, Track 1). Separate subsection; do not merge with the
     Character Catalog / Combat / Hitbox entries above. -->

### Frame Cues

- **Frame Cues now carry concrete playback/spatial context and exact terminal semantics
  (TASK-118.1.4, 2026-08-03).** `FPaper2DPlusFrameCueContext` exposes the Character Profile,
  registered playback component, and an explicit runtime/editor evaluation mode; the shared
  `ResolveFrameCueAnchor` Blueprint helper resolves render-origin or base-Profile socket transforms
  with exact failure results and keep-world attachment semantics. The Profile component now exposes
  an optional external-playback source/generation seam for PaperZD and other players, automatically
  observes both custom and stock flipbook playback, and reports one concrete End reason for each begun
  Cue State. Natural completion samples the final frame before ending; stop-then-play transitions remain
  two observable source events rather than a fabricated uninterrupted transition. Editor playback,
  scrub/seek, selection preview, and reset use the same context/resolver/terminal contract as runtime.
  Cue behavior remains synchronous: authoring, compile/readiness, save, durable validation, and cook now
  reject latent, async-action/task, timer, and self-scheduling paths recursively through collapsed
  graphs, macros, and specialized Cue parents. This is additive, does not require or expose a PaperZD
  AnimInstance, and does not change the durable Cue Type schema or fingerprint.

- **BREAKING — Cue timing forms are now Cue and Cue State (TASK-170, 2026-08-01).**

  | Previous public class | v8 public class | Designer label |
  |---|---|---|
  | `UPaper2DPlusFrameCue` | `UPaper2DPlusCueBase` | hidden abstract base |
  | `UPaper2DPlusMomentCue` | `UPaper2DPlusCue` | **Cue** |
  | `UPaper2DPlusRangeCue` | `UPaper2DPlusCueState` | **Cue State** |

  Existing Cue Type parents and instanced Profile/Layer placements load through three direct
  plugin-local CoreRedirects; no redirect chains or `MatchSubstring` catchall are shipped. Existing
  durable Cue Type baselines also stay byte-identical: only the durable-schema canonicalizer continues
  to emit the retired native path tokens, so loading does not dirty or restage an asset. C++ and
  Blueprint code that names any previous class must be updated or refreshed to the v8 symbol. The
  `FrameCues/` folder and filenames, **Frame Cues** tab and IDs, `OnFrameCue`, and
  `OnCueTriggered` / `OnCueBegin` / `OnCueUpdate` / `OnCueEnd` names are unchanged.

- **BREAKING — Frame Cue objects now run their own behavior (2026-07-31).** A Cue Type may implement
  **On Cue Triggered** (Cue) or **On Cue Begin** / **On Cue Update** / **On Cue End** (Cue State) in the
  restricted Frame Cue Type editor's one permitted event graph, and dispatch runs that behavior
  immediately **before** the `OnFrameCue` listener broadcast at every site — ordinary frame changes,
  every forced teardown (EndPlay / component destruction, animation change, the equipment sweep), and
  editor preview. The cue acts, then the world reacts. This deliberately reverses the 7.0.0 rule that
  "Frame Cues are data, receivers own behavior": that rule cost the notify-style workflow designers
  actually wanted, and the six native built-in Cue types existed largely to paper over that gap — which
  is why they are deleted in this same cut (see below). It is a considered reversal, not drift, and the
  repo's written conventions were rewritten in the same change.
  **Behavior must be stateless.** It executes on the **shared, asset-owned placement instance** — two
  characters playing the same animation, and one character dispatched through two components, call it on
  the same object — so an implementation must read that placement's payload plus the notification
  Context, act on the world, and never write instance state. Persistent state and every release stay on
  the receiving actor or component.
  A Cue Type that implements nothing behaves exactly as it did in 7.0.0. The envelope is otherwise
  unchanged: it now admits the declared event overrides plus the compiler's ubergraph function and its
  generated frame property, and still rejects — at authoring, at compiled save, and at cook — a second
  event graph page, function/macro/delegate-signature graphs, any generated function that is not a
  declared Cue event, components and construction scripts, timelines, Blueprint interfaces, delegate
  payload fields, Blueprint getter/setter fields, replication/RepNotify, non-persistent field flags, and
  generated fields with no authored counterpart.

- **BREAKING — the six built-in Frame Cue classes are DELETED. No migration, no CoreRedirects
  (2026-07-31, TASK-165).** `UPaper2DPlusPlaySoundCue`, `UPaper2DPlusSpawnEffectCue`,
  `UPaper2DPlusSpawnProjectileCue`, `UPaper2DPlusCameraShakeCue`, `UPaper2DPlusScreenFlashCue` (one-shot)
  and `UPaper2DPlusApplyGameplayTagCue` (state) are gone, together with the header and source file that
  declared them (`FrameCues/Paper2DPlusBuiltInFrameCues.h` / `.cpp`). These classes never shipped
  carrying behavior — an earlier draft of this same unreleased section announced that they would, and
  that announcement is superseded and withdrawn by this entry. **As of this entry Paper2DPlus
  shipped no concrete runtime gameplay Cue class** — `UPaper2DPlusCueBase`, `UPaper2DPlusCue`, and
  `UPaper2DPlusCueState` are all `Abstract`, and every designer-placeable Cue is a designer-authored
  Cue Type. (Superseded within this same cut: the later 2026-08-08 Spawn Flipbook Cue entry ships
  exactly ONE native concrete cue in the deleted six's place — a deliberate, user-approved
  reversal; the three bases stay abstract.) Editor-only `HideDropdown` automation/visual-tour
  fixtures are excluded from designer lists and cooked data.
  **Existing placements of the six stop firing.** A placement whose class no longer exists resolves to
  nothing: it is never dispatched, it never broadcasts to `OnFrameCue`, and it is reported by the same
  validation and "placements dispatch cannot deliver" runtime diagnostic that already covered a
  placement whose Cue Type asset was deleted. **Re-author each one as a Cue Type of your own** — the
  payload fields were ordinary reflected properties and the behavior hooks are exactly the
  `OnCueTriggered` / `OnCueBegin` / `OnCueUpdate` / `OnCueEnd` events a Cue Type overrides. **No
  `CoreRedirects` row is provided, and this is deliberate**: `Config/DefaultPaper2DPlus.ini` gains
  nothing for these six names. A redirect pointing at a class that no longer exists helps nobody, and a
  redirect onto a surviving Cue class would silently reinterpret one Cue's saved payload as another's.
  Nothing erases the dead placement rows from your assets; they simply sit there inert until you
  replace them.
  **Removed with them — legacy conversion is over, legacy data is not destroyed.** The Frame Event →
  Frame Cue conversion bridge and the legacy `Effects` / direct-data Spawn Effect migrations
  (`MigrateEffectsToFrameEvents`, `MigrateFrameEventsToCues`, `MigrateSpawnEffectCuesToDirectData`, each
  of which ran from both `PostLoad` and JSON import) are deleted outright rather than gutted, because
  every one of them produced a built-in and with the built-ins gone they produce nothing. Legacy data of
  those shapes — `FFlipbookProfileEntry::Effects_DEPRECATED`, the retained executable Frame Event
  inventory — is now **inert rather than auto-converted**. It is **not** deleted from your assets and
  those containers are deliberately left untouched: draining them while removing the migration would
  destroy the only remaining copy of that legacy authoring data on the next save.
  **Preview adapters:** the five bespoke *native* preview adapters are gone. The preview **adapter
  framework** is untouched and remains a supported extensibility point — the abstract
  `UPaper2DPlusFrameCuePreviewAdapter`, the preview host, the preview context, the zero-resource
  cleanup ledger, and the `UPaper2DPlusFrameCueEditorSettings::PreviewAdapters` project registration
  array all stay. Register your own adapter class there when a Cue Type needs bespoke preview rendering.
  **Retired validation codes:** the four Effect-specific `EPaper2DPlusFrameCueValidationCode`
  enumerators `UnresolvedLegacyEffectSource`, `MissingEffectFlipbook`, `ZeroEffectScale`, and
  `PendingEffectFlipbookDiscovery` (their only producer was the deleted Spawn Effect Cue path), plus the
  FName code `Paper2DPlus.Character.FrameCue.EffectOutOfLibrary`. TASK-167 subsequently retired the
  generic `Paper2DPlus.Character.FrameCue.EffectFieldOutOfLibrary` and
  `Paper2DPlus.Character.FrameCue.EffectFieldFilterMismatch` codes with the Cue Type Effect-field
  schema itself.
  **The delta-serialization warning still applies — to the Cue Types you author.** Net Policy is an
  ordinary reflected property and therefore delta-serialized: a placement whose value equals its class
  default is not written into the asset at all and re-reads that default on the next load. Editing a Cue
  Type's class default therefore silently rebases every already-saved placement that accepted it, with
  no diagnostic and no undo. A default that genuinely has to change needs a versioned `PostLoad`
  migration that reads the old default into an explicit stored value first — never a bare edit to the
  class default. What is gone is the *frozen native defaults list* this entry used to pin, because there
  are no native cue classes left to pin. New Cue Types are still created **Cosmetic Only**; choose
  Authority Only yourself for anything that mutates gameplay state, since a cosmetic projectile spawner
  would run on clients while skipping the server.

- **Frame Cue Type durable schema is version 2, with verbatim version 1 compatibility.** A Cue Type that
  carries no behavior still describes itself with the byte-identical version 1 canonical text and
  fingerprint, so every Cue Type authored under 7.0.0 stays loadable, still classifies as
  placement-ready, and re-saves cleanly with no restaging pass. Only a type that actually implements a
  behavior event is stamped version 2.

- **New Cue API, additive.** `PlayCueSound` is a context-aware Blueprint sound helper on
  `UPaper2DPlusCueBase`: in game it plays at the owning actor's location and returns false rather than
  pretending on a dedicated server or with no actor; in the Frame Cues tab the same call intentionally
  routes to editor-owned audio in the preview resource ledger, so seek/stop/close deterministically
  silence an authored footstep. Ordinary world-aware audio nodes also work during the synchronous
  callback through the isolated preview world. `CollectWarmableEffectArt` is a new overridable
  virtual: the frame-zero warm pass now asks each placement what art it declared instead of testing for
  known Cue classes, so every placement — including a Cue Type you author — is warmed on the same terms.
  Its base implementation structurally reads every scalar soft Paper Flipbook property, resolved or
  unresolved. TASK-167 removed the editor-only marker pass and its authoring schema; with the native
  cue classes deleted, this cooked-safe class-agnostic discovery is the *only* warm path there is.

- **BREAKING for scripted callers — new Cue Types are placement-ready at creation, and default to
  Cosmetic Only.** Both creation flows now finish the durable-schema staging and the protected package
  save that placement classification requires, so a type created from the timeline or from the Content
  Browser appears in **+ Add Cue** in the same session without a visit to the restricted editor's Save;
  the Content Browser route defers that save by one tick, after the inline rename has committed, so it
  leaves no redirector behind. New types are also created **Cosmetic Only** rather than inheriting the
  runtime base's Local Always, which would have run every authored cosmetic on a dedicated server with no
  designer action. Anything scripting `FPaper2DPlusFrameCueTypeAuthoring::CreateCueType` therefore gets a
  Cosmetic Only class default where it previously got Local Always. `CreateCueType` itself still does not
  save, so a script that wants a placement-ready asset must run the new birth-ready service the way the
  two UI flows do.

- **The + Add Cue picker shows Cue Types it cannot place instead of hiding them.** A type that is
  uncompiled, unsaved, quarantined, or unloadable now appears as a disabled row carrying the reason it
  cannot be placed, with a click-through that opens it in the restricted Frame Cue Type editor. Silent
  hiding was the root cause of "I created a Cue Type and it never appeared". Structural rejections — the
  abstract Cue/Cue State parents and the skeleton/reinstancing/trash classes a loaded Cue Type drags along
  — remain unlisted, and the picker's old aggregate status line became a plain empty state.

- **BREAKING for tick cost — actors that carry Frame Cues are watched every frame instead of at 20 Hz.**
  Detection for a stock `UPaperFlipbookComponent` was sampled at 20 Hz, a rate sized for root motion,
  where a late sample costs 50 ms of delay and nothing else. A Frame Cue anchor the sample never sees is
  not late, it is gone: an animation that starts and finishes inside one 50 ms window is never observed,
  so none of its cues ever fire. An actor whose Character Profile — or whose equipped Character Layer —
  carries any Frame Cue is now sampled every frame (roughly 3x the detection ticks at 60 fps for those
  actors); an actor with no cues anywhere keeps the 20 Hz watch, and the event-driven
  `UPaper2DPlusFlipbookComponent` path still costs no tick at all.
  An animation with cues now either dispatches or says why not. Two diagnostics, each latched once per
  Character Profile Component — on screen in PIE and development builds, plus a `LogPaper2DPlus`
  warning — name the actor, component, animation, and profile: one for a poll too coarse for the
  animation's wall-clock life (authored duration divided by the magnitude of the play rate), one for
  placements dispatch cannot deliver (empty, or their Cue Type asset deleted). Both are deliberately
  conservative: the coarse-poll warning judges the **requested** poll interval rather than one observed
  frame delta, so a hitch cannot latch a permanent false alarm — which also means sampling loss caused
  by a low frame rate is out of scope and will not warn. The old process-wide "stock component" warning
  became a per-actor Log line that names the actor.

- **Two observable Cue State lifecycle details.** The equipment sweep that runs when a Layer is equipped
  or removed can deliver **On Cue Update** (and the matching listener Update) to a still-present,
  still-in-range cue with no frame change — pre-existing listener behavior, now mirrored to behavior
  events; read Update as "still active", never as "one frame passed". At a loop wrap, a still-active
  range whose Cue Type asset has been deleted now ends with `SourceRemoved` rather than `LoopReset`.
  Separately, a range whose Cue Type asset dies mid-flight still receives its paired **listener** End but
  no **behavior** End, because behavior cannot run on a placement whose class is gone — which is why
  releases belong on the receiver.

- **BREAKING for Paper2DPlusEditor C++ callers: Frame Cues now defaults to pick a frame, add a Cue,
  edit it, preview it (TASK-151, 2026-08-01).** The persistent
  **+ Add Track** button is retired into one **Manage tracks...** overflow that owns create, rename,
  reorder, remove, and the choice of which track **+ Add Cue** targets (choosing that target selects the
  track, which clears the current Cue selection, the same side effect as clicking a track header). The
  frame strip, Cue lanes, and curve graph now share one compact bounded resizable left gutter, so their
  timing columns remain aligned without surrendering the editor to long labels. Cue and curve selection
  happens directly on the timeline; the redundant indexed **Cues** category is retired, curve rows show
  only color/name identity, and the surviving **Details** category switches between Cue payload/actions
  and curve visibility, solo, interpolation, rename, orphan repair, and removal. The exported
  `FOnCurveTrackModeChanged` / `FOnCurveTrackAction` delegates, their `SCurveTrackRow` Slate events,
  and `SCurveTrackStack::HasRenameAffordanceForTests` are removed; integrations should select the
  curve and invoke the stack's mutation API from their own contextual Details surface.
  Migration bookkeeping — the legacy Frame Event flag and its receiver acknowledgement — now appears
  only for a placement that actually came from a Frame Event import, per placement rather than per asset,
  so a clean sibling stays clean. Network policy and Cue State end reasons are described in plain language in
  the details-pane summary and are pinned never to appear as timeline chrome.

- **Editor preview executes cue behavior in an isolated world, and contains its failures.** Preview runs
  behavior through the same helper as the game against an editor-owned actor, Profile Component, and
  `EditorPreview` world, then notifies any optional preview adapters. `Is Editor Preview` is the stable
  preview sentinel; Cue `GetWorld()` resolves only for the synchronous behavior callback and the shared
  asset-owned placement is worldless again afterward. Ordinary world-aware nodes can therefore draw
  traces, spawn actors/effects, and play sounds directly. `PlayCueSound` remains the deterministic
  teardown-safe helper for audio. The first failure of a given **placement** in an editor session logs
  once and badges that placement on the timeline; preview keeps dispatching, the same behavior runs
  again on the next crossing, and a sibling placement still reports its own failure. Behavior is never
  quarantined, and nothing wraps, badges, or suppresses it in game. With the six native built-ins
  deleted, **no Cue ships or requires a preview adapter**: the Cue Type's ordinary behavior is its
  preview implementation. Register an adapter only for an advanced custom overlay; the host's resource
  ledger still owns everything an adapter creates.

## v7.0.0 — 2026-07-26

The last cut was v6.2 (2026-04-29). Everything below has accumulated since. The Data Model /
Deprecations entries alone carry nine independent MAJOR triggers — retyped Blueprint pins, deleted
node families, removed features, and asset schema migrations — so this cut is 7.0.0, not 6.3.

**Not in this release: Animation Map Fragments.** The reusable-Fragment feature is compiled into the
editor module and its tests run, but every user-reachable surface is withheld — there is no New-menu
entry, no Content Browser type, no Add / Refresh Fragment, no Bind Slot or Remove Instance, and no
details section. Consequently no Fragment asset can be created and no Fragment data can exist. It is
called out here rather than passed over in silence because the classes are present in the binary and
discoverable. The feature was never exposed in a shipped version, so nothing is being taken away from
anyone. It is held back because it still needs work and produced real instability, including an
editor crash when a Fragment asset was deleted from the Content Browser. Development continues on a
branch; a future release will either ship it properly or remove it outright.

### Features

- **Playback Queue is a docked tab, and arrow-key stepping no longer sticks (2026-07-25).** The queue moved out of the Navigator's collapsed expander — itself inside a closed-by-default tab — into its own **Playback Queue** tab sharing the lower-right stack with **Completion** and **Related Profiles**, in both the Character Profile and Layer workspaces (layouts bump to `CharacterProfileAssetEditor_Layout_v14_PlaybackQueue` and `CharacterLayerAssetEditor_Layout_v13_PlaybackQueue`; saved arrangements reset once). `Paper2DPlus.SwitchTab queue` opens it, and adding from the Navigator reveals it. **The stepping fix:** the queue cursor and the animation selection were two independently maintained positions, so selecting an animation any other way (browser card, Navigator, Animation Map node, undo) left the cursor pointing at an unrelated slot — the next arrow press then computed "next entry" from that stale slot and either parked on one animation forever or jumped somewhere unrelated. The cursor is now reconciled against the selection, and all six tool tabs route through one model entry point (`StepQueue`) instead of six subtly different hand-rolled copies. Consequences: Up/Down wrap at the queue ends instead of clamping (they used to dead-end on the last entry); Left/Right at a frame boundary always either step the queue or wrap inside the current animation — three tabs previously swallowed the key and pinned the playhead to the last frame; hopping entries always lands on the destination's frame 0 (or its true last frame going backwards), where two tabs previously carried the old frame index over; a single-entry queue and an off-queue selection now fall back to plain within-animation wrapping instead of re-selecting the animation already on screen; and an animation queued twice can be stepped past instead of snapping back to its first occurrence.

- **Automatic, event-driven hit detection (TASK-145, 2026-07-22).** Opt-in `bAutoHitDetection` on the Character Profile Component turns the authored attack boxes into the detection driver: entering a key frame that carries attack boxes arms a detection window (`OnAttackWindowBegin`), the component queries the hitbox subsystem at every armed frame entry AND between key-frame transitions (the subsystem is now a conditional tickable that re-checks armed attackers — zero cost while nobody is attacking), hits dedup once per victim per move instance per `HitWindow`-curve window through the same ledger as `ValidateAndRegisterHit`, and results broadcast `OnHitConnected` (attacker side) plus `OnHitReceived` (victim side) with a complete `FPaper2DPlusAutoHitResult` payload — victim, damage, knockback, boxes, hit location, move name, frame, window. `OnAttackWindowEnd` and `OnAttackWhiffed` (armed but landed nothing) complete the surface. Multi-target is inherent: every overlapped victim registers independently, and latecomers who walk into a held active frame connect via the armed-set tick. The plugin never applies damage — receivers do. Authority-gated (Standalone/Authority only; proxies never arm — cosmetics ride Frame Cues); default off, so the existing pull APIs (`CheckAttackCollision`, `QuickHitCheck`, `ValidateAndRegisterHit`) are unaffected and remain for off-animation cases.

- **Effect art loading is lazy and Blueprint-transparent (TASK-143, 2026-07-21).** Effect Profile rows and native Spawn Effect Cue Flipbooks now serialize as internal soft references, so loading an Effect Profile loads no library art and loading a resaved Character Profile loads no native Spawn Effect Cue art. `GetEffectEntryCount` supplies a no-load Blueprint iteration bound; indexed, compatibility-row-break, and Cue resolution load one Flipbook, filtered array queries select from metadata first and load only their matches, and `GetEffectFlipbooks` remains the explicit broad operation that loads every unique member. Asset Registry validation and normalized-path membership remain no-load, follow redirectors, reject missing/wrong-class targets, surface discovery-unknown paths as provisional issues, and refresh editor projections when discovery completes. Blueprint never receives a raw soft pin: current queries/`ResolveSpawnSettings`, the read-only legacy `Effects` array's native Break Struct boundary, and the old native Cue property getter all return ordinary hard `UPaperFlipbook*` values. The Character component and Frame Cues editor warm and retain only the current animation's native cue effects before frame-zero dispatch, keeping per-frame dispatch and Slate paint load-free. UE 5.0–5.8 converts old Effect-row hard values in place; a native Cue's old hard import—including Blueprint-derived Cue defaults—passes through a deprecated compatibility property once, then resave removes it. Effect Profile schema v2 and native Cue effect-reference schema v1 mark upgraded owners for resave, while Spawn Effect Cue `DirectDataVersion` remains unchanged. TASK-167 later removed the special hard Cue Type Effect-field path; ordinary scalar soft Paper Flipbook variables now use the structural warm pass.

- **Character Catalog rows can be removed without deleting assets (2026-07-22).** The selected Character's Details now offers **Remove from Catalog…** with an explicit confirmation that the Character Profile and companion assets are retained. Removal is one undoable transaction, prunes active ordered Group memberships, records the authored roster/Group positions in **Advanced > Orphan Recovery**, and persists as a deliberate project-scan exclusion until restored.
- **Combo Blueprint nodes now have their own action-menu submenu (2026-07-19).** All six combo-specific Animation Map nodes live under `Paper2DPlus > Animation Map > Combo`; exact-tag lookup and the two transition nodes remain directly under `Paper2DPlus > Animation Map`.
- **Animation Map bulk authoring and stable graph editing (2026-07-19).** A real move multi-selection can now change Group, Phase Tag, or the complete Animation Tags container in one undoable operation; dragging a card connection onto empty canvas offers linked Chain Start / Chain End marker placement. Moving an animation to another group clears only that animation's old placement so it auto-lays out against the destination group, while same-group no-ops and undo retain/restore the prior coordinates. Comment boxes preserve their persisted node identity across projection rebuilds, support F2 inline rename, and move/resize/delete without respawning. Comment reconciliation and ordinary card/tag/phase/group writes use granular node actions instead of default whole-graph notifications, so unrelated flipbook cards and thumbnails no longer flash when editor-only state changes.
- **Chain Ends bound the countable combo line; chains gain their own lookup identity and direct length nodes (2026-07-19).** Each mapping entry now also carries a boolean **Chain End** flag (`FFlipbookTagMappingEntry.bIsChainEnd`): the combo main line's last COUNTABLE step. The derived spine (`Paper2DPlusComboChain::DeriveComboSpine`) always prefers a path TERMINATING at a flagged end over any longer non-end path and never walks past an end, so trailing recovery/settle animations wired after the end stay authored and reachable but are excluded from combo indexing and length; no end flagged keeps the plain longest-line behavior. Chain Start entries can additionally author **Chain Tags** (`FFlipbookTagMappingEntry.ChainTags`, `Paper2DPlus.Animation` category) — the CHAIN's own identity container. The ByTags chain nodes now resolve with PRECEDENCE: exactly one Chain Start whose authored container exactly equals the query wins; only when no chain container matches do they fall back to an animation's own `AnimationTags` exact match; multiple matches in the winning dimension fail as `AmbiguousInput`. The Blueprint surface grows to EIGHT nodes with the direct length queries **`Get Combo Chain Length`** / **`Get Combo Chain Length by Exact Tags`** (opener flipbook or chain container in, the countable start→end length out; failure clears the output). Authoring: a mirrored **Chain End marker node** (right-click canvas → Add Chain End; flagged moves render a small ⏹, the ▶'s slate-blue mirror), a "Chain end" checkbox plus a compact chain-tags editor beside the details pane's "Chain start" checkbox, and the graph probe adds a `,end` node marker plus `chainend -> <target|floating> @x,y` marker lines. Both new fields round-trip JSON export/import. **`Has Combo Chain(Profile, Flipbook)`** completes the set: the cheap boolean "does this attack open a combo?" branch — true only for a flagged opener whose countable line has 2+ steps; single animations, mid-chain members, one-move chains, and every failure mode return false.
- **Chain Starts replace numbered roots; the Blueprint surface shrinks to six flipbook/tag-keyed nodes (2026-07-18) — BREAKING (supersedes the Root Number model below).** `FFlipbookTagMappingEntry.RootNumber` is now the boolean **Chain Start** flag (`bIsChainStart`): chains are keyed by (group tag, opener move name) and named by their opener, traversal still stops at every other flagged start, and multiple flags per group simply mean multiple chains — the duplicate/invalid Root Number failure modes no longer exist (an unresolvable flagged Chain Start is still an Error). In the Animation Map, a small **Chain Start marker node** authors the flag: right-click the canvas → Add Chain Start, wire its single output into a move to make that move the chain opener, re-aim the wire to move the start, and break the wire or delete the marker to clear the flag — the marker is a projection of the entry flag, and the flat data stays the single source of truth. The move node's `R#` badge becomes a small decorative ▶ on flagged moves, and the details pane's per-entry "Root #" spinner becomes a "Chain start" checkbox. Migration is idempotent at load/JSON import via `MigrateRootNumbersToChainStarts`: any positive legacy Root Number becomes the flag (the number carried no meaning beyond identity), and schema-7 JSON global root flags now migrate straight to chain-start flags. The Blueprint surface is now SIX nodes with zero reference structs — `FindAnimationByExactTags`, `GetAnimationTransitionInfo`, `ResolveAnimationTransition`, `GetComboChainFlipbookAtIndex`, `GetComboChainFlipbookAtIndexByTags`, plus the new **`GetComboOpenerFlipbooks(Profile)`** (every valid Chain Start's opener flipbook, group-tag-sorted then authored entry order, unloadable entries skipped). DELETED: `GetAnimationGroupTags`, `FindAnimationGroup`, `GetAnimationRootNumbers`, `FindAnimationRoot`, and the `FPaper2DPlusAnimationGroupRef` struct — everything is keyed by Profile + flipbook or exact tags.
- **One combo step by index: `Get Combo Chain Flipbook at Index` / `... by Exact Tags` (2026-07-18/19; the interim array-returning shape was replaced same-session — no node returns flipbook arrays).** Put in the chain's flagged opener — a flipbook or the tag container identifying it — plus a game-owned combo counter, and get exactly that step's flipbook out (opener = index 0) together with the chain length; Index Out Of Range is the explicit "combo finished" signal, replacing phase-by-phase transition walking for combos. The main line is auto-derived (`Paper2DPlusComboChain::DeriveComboSpine`): the LONGEST simple authored confirm path from the root, with authored transition-row order breaking ties — so a shortcut-to-finisher cancel edge never hijacks the numbering — and it is deterministic, cycle-safe, group-bounded, and stops at other numbered roots. Side branches stay reachable via `ResolveAnimationTransition` criteria and report index -1; an animation reachable from multiple numbered roots fails closed as Ambiguous Chain. The editor surfaces the same derivation: Animation Map chain nodes carry a `#N` step chip beside the R# badge, the details pane shows a read-only "Combo: index N of M (Group R#)" line (or "branch of…"), and the graph probe gains a `,combo=N/M` marker — all stamped from the one shared spine derivation, so the editor can never disagree with gameplay. (Root Numbers themselves were later demoted to Chain Start flags and chains are now named by their opener — see the topmost entry; the indexed-spine semantics are otherwise unchanged.)
- **Transition inspection/resolution is now flipbook-keyed; roots are discovery-only (2026-07-18) — BREAKING (supersedes the pipeline shape below).** `GetAnimationTransitionInfo` and `ResolveAnimationTransition` now take the Character Profile plus the current flipbook only — no Group or Root inputs. A flipbook is unique within its map, so it is the sole resolver key: it must resolve to exactly one map entry (a flipbook referenced by multiple entries fails closed as the new `AmbiguousFlipbook` result), and every authored direct From → To row is visible regardless of group membership or root boundaries. Selection criteria now match each target's EFFECTIVE animation tags (own ∪ chain-inherited ∪ group tags), so a group tag in criteria still scopes candidates without a Group input. `FindAnimationRoot` returns just the opener flipbook (`FPaper2DPlusAnimationRootRef` is deleted — its Group/RootNumber outputs merely echoed the inputs), leaving Group → Root as a pure discovery route for chain openers. `EPaper2DPlusAnimationResolveResult` replaces `GroupNotFound`/`RootNotFound`/`OriginalOutsideRoot` with `FlipbookNotInMap`/`AmbiguousFlipbook`. Blueprints using the old node signatures must be re-wired. (Root Numbers themselves were later demoted to Chain Start flags and the Group → Root discovery route was deleted in favor of `GetComboOpenerFlipbooks` — see the topmost entry.)
- **Exact Animation Tags are now the primary Animation Map lookup; roots are optional (TASK-125.1).** The new pure Blueprint node `Find Animation by Exact Tags` resolves one live flipbook whose complete own authored `AnimationTags` container exactly equals the query, independent of tag order and without requiring a Group or Root. Subsets and parent/child hierarchy do not match; invalid, missing, and ambiguous requests return explicit results and clear the output. Standalone duplicate containers validate as errors. A duplicate set entirely inside one valid numbered-root chain is allowed for chain authoring but remains ambiguous through tag lookup, so gameplay must use the Group → Root route there. Ordinary mappings remain `RootNumber=0`; positive values are deliberate chain identities and are never synthesized when assigning or moving a flipbook into a group. (Root Numbers themselves were later demoted to Chain Start flags — see the topmost entry; the exact-tags lookup is unchanged.)
- **Animation Map Blueprint access is now one exact Group → Root Number → transition pipeline (TASK-125) — BREAKING.** Each tag-mapping entry may author a positive group-local **Root Number** (`0` is non-root; gaps are valid; the same number may be reused in another group). The editor projects `R#` badges, shows each group's validated root count, and validates uniqueness plus duplicate membership. Blueprint gameplay dynamically discovers or selects an exact group, dynamically discovers or selects its root, inspects valid direct exits/phases, then resolves exactly one target from Original Flipbook + requested phase + optional Required All/Any/Excluded criteria. `FindAnimationGroup.OutGroup.MemberCount` preserves the validated scalar group size without exposing raw mapping entries; root-array length is never used as a member count. Traversal is cycle-safe, cannot cross the selected group, and stops at another numbered root; duplicate/invalid data and ambiguous matches fail explicitly. The prior 23-node tag/combo/phase/raw-mapping/bare-target surface, global combo-root flag, tag/index PaperZD resolver, and redirects were deleted without deprecation; affected LostRadiance Blueprints and profile assets were migrated in place, including the Combo Component member count and Blueprint function inputs renamed from Array Index to Root Number. Schema-7 JSON imports translate deleted global root flags to positive group-local numbers in authored mapping order while leaving unflagged rows at `0`. Feed a resolved flipbook to the existing flipbook-keyed PaperZD resolver when a sequence is needed. (Root Numbers themselves were later demoted to Chain Start flags and the Group → Root node family was deleted — see the topmost entry.)
- **Designer workflow overhaul — one quiet visual grammar and one validation language.** Character, Layer, Effect, Combat, and Catalog editors now follow the Character Profile's compact card/section hierarchy, stable selection model, and contextual-tool conventions. Character layout `CharacterProfileAssetEditor_Layout_v12_CompactValidation` keeps six main tools; Layer uses `CharacterLayerAssetEditor_Layout_v9_CompactWorkspace`; Effect uses `EffectProfileAssetEditor_Layout_v3_CharacterGrammar`; Combat uses `CombatProfileAssetEditor_Layout_v6_CompactWorkspace`; Catalog uses `CharacterCatalogAssetEditor_Layout_v2_CompactTabs`. Validation from an editor, Content Browser, Unreal Data Validation, and `Paper2DPlusValidate` is projected through one adapter service. Editor Validate actions open the shared modeless issue panel instead of reserving permanent Validation tabs, while deterministic issue identity, severity, focus targets, relationship checks, and machine-readable reports remain intact.
- **Character Layer authoring is layer-first and publish-safe.** The Layer editor's compact layout `CharacterLayerAssetEditor_Layout_v9_CompactWorkspace` uses a one-line related context, animation drawer, structure tree, Art/Hitboxes/Frame Cues inspectors, shared profile navigation, and persistent layer/group selection. Publishing no longer reserves a top status banner: one green **Bake** button sits at the bottom right and opens status, diagnostics, and every currently valid Adopt/Bake/Save/Repair/Rebase/Detach action on demand. Fixed appearances publish through an overwrite-only canonical bake that preserves the original flipbook identity, timing, pivots, offsets, non-managed data, hitboxes, and Cues; staging, manifests, recovery checkpoints, adoption analysis, exact output ownership, cancellation, rollback, and reimport preservation make failed or externally changed output explicit instead of silently destructive.
- **Explicit Layer delivery modes and scalable runtime appearance.** A Layer asset now chooses Legacy Runtime Layers, Canonical Bake, or Runtime Customizable delivery. Runtime customization replicates only a canonical descriptor of committed Skin/hair/wearable/Cosmetic Effect/Outfit intent; gameplay composition is authoritative and independent of rendering. The opt-in hybrid renderer displays an immediate bounded live result, builds proof-gated transient GPU composites asynchronously, hands off only at a safe frame, and falls back predictably under cache pressure. A world budget subsystem assigns near/far/off-screen tiers for crowds, enforces primitive/cache/work caps, exposes Blueprint priority overrides and read-only diagnostics, and destroys off-screen visual ownership. No runtime path creates, mutates, saves, or replicates a persistent appearance asset.
- **Historical v7 behavior, superseded by the v8 TASK-165 entry above — Frame Cues replace executable Frame Event authoring.** Moment and Range Cues are serialized data with stable identities, deterministic skipped-frame/range lifecycle dispatch, typed Blueprint listener delivery, native queries, layer-aware composition, preview-host adapters with a zero-resource cleanup ledger, and network catch-up guarantees. The editor offers type-driven data authoring and creator preview while PaperZD remains playback-only. At the time, legacy native/custom event fixtures migrated through sanctioned bridges with diagnostics and Spawn Effect migration preserved resolved values; v8 deleted those bridges and leaves the retained rows inert.
- **Historical v7 Cue behavior, superseded by the v8 TASK-165 entry above — Effect Profile is a character-agnostic visual library.** Layout `EffectProfileAssetEditor_Layout_v3_CharacterGrammar` puts Library (36%), Preview (38%), and Details (26%) left-to-right with Advanced closed. Preview uses the Character-style frame strip, never autoplays, and has no visible transport/loop/slider clutter: Space toggles playback, frame click pauses/seeks, and Left/Right/Home/End provide direct keyboard seeking. A visible focus outline, selected-cell auto-scroll, and live accessible effect/frame/playback summary keep the compact interaction explicit. Entries remain exact flipbook members (now soft internally) with native effect-category tags, searchable/bulk-editable cards, persistent selection, and shared modeless validation. At the time, native Spawn Effect Cues stored exact flipbook identity softly plus local values and legacy profile/name/override data migrated idempotently; v8 deleted that Cue class and migration while leaving the Effect Profile asset system intact.
- **Character Catalog is the authoritative character universe.** Layout `CharacterCatalogAssetEditor_Layout_v2_CompactTabs` presents Roster as a virtualized Character-overview-style card grid with focused Details on the right, Groups as the second primary tab, and Advanced closed. Cards use designer-facing names and concise statuses rather than raw IDs or a heavy selection overlay. Visible unloaded art resolves asynchronously through a true-LRU capped at 48, while explicit Refresh invalidates/retries presentation loads. Groups restores group/member selection by stable value after reorder and disables impossible boundary moves. `UPaper2DPlusCharacterCatalogAsset` discovers profiles within configured roots, owns unique Layer/Combat relationships and authored Effect assignments, preserves ordered tags/groups, Syncs deterministic soft entries, audits discovery/relationship/completion/cook drift, and exposes soft-first packaged Blueprint queries without implicitly loading linked profiles. LostRadiance includes the required Primary Asset scan and AlwaysCook host rule. Production automation uses `scripts/sync-paper2dplus-character-catalog.ps1` → `Paper2DPlus.Catalog.SyncConfiguredAndSave`, the same production discovery/Sync adapter as the editor: it saves only an applied diff to the configured Catalog, performs a hash-safe no-op otherwise, and returns `0` for clean/warnings, `1` for validation errors, or `2` for configuration/Sync/save failure. This seam is deliberately a file-scope console command, not a second commandlet.
- **Combat authoring uses stable move identity and reproducible scenarios.** Layout `CombatProfileAssetEditor_Layout_v6_CompactWorkspace` applies compact cards/headers and modeless validation across Setup, Attack Catalog, Score Playground, Combat Lab, and tuning collections. Attack cards are virtualized and load visible soft flipbooks asynchronously through a bounded true LRU. Combat Lab click-selects and focuses a participant for arrow positioning (10 units, Shift 1, Ctrl 50) in addition to dragging. Object-backed attack options survive flipbook renames, name-only compatibility rows remain diagnosable, and guided tuning is paired with deterministic Score Playground/Combat Lab scenario evaluation through the same advisory scoring helpers used at runtime.
- **Wearable categories — tag-based "slots" with per-category exclusivity (TASK-112, additive).** Wearable-kind variants can carry an optional `CategoryTag` under the new native `Paper2DPlus.Wearable.*` taxonomy (starters: `Hat`, `Backpack`, `Cape`; extend via the tag picker). **The direct child of `Paper2DPlus.Wearable` is the slot**: equipping a categorized wearable auto-unequips the same-category wearable on that part — identically in the editor Wardrobe, the runtime `AddWearable`, outfit apply (last-wins per entry), the preview overlay, and spawn-default resolution, all through one shared decision helper (`Paper2DPlusWearableCategories`). Deeper tags refine within a slot (`Hat.Wizard` is still a Hat). The Wardrobe groups a part's wearables by category (uncategorized last, flat as before); the validator warns on duplicate-category defaults/outfit entries. Untagged wearables stack freely exactly as before — no schema bump, no replication changes (the rule runs on the authority mutation paths; snapshots stay verbatim).

### Data Model / Deprecations

- **UPGRADE STEP — open and resave your Paper2DPlus assets on 6.2 before moving to 7.0.0.** The
  load-time migration bridges that convert pre-6.x data are still present in 7.0.0 and still run, so
  nothing breaks the moment you upgrade. They are scheduled for removal in **8.0.0**. Once a
  `UPROPERTY` is deleted, Unreal silently ignores that tag on load — there is no warning and no way
  to detect it — so an asset that still carries legacy values and has never been resaved would lose
  them without an error. Resaving on 6.2 folds every legacy value into its current field and makes
  the eventual 8.0.0 removal a no-op. Affected data if you skip it: per-frame hitbox rows, excluded
  frames, root motion, chain identity (legacy Root Numbers), PaperZD sequence links, effect rows,
  and combat variable identity.

- **No `_DEPRECATED` field was removed in 7.0.0 — deliberately.** A cleanup pass audited all of them
  and removed none. Roughly nineteen are live `PostLoad` migration bridges. Of the two that looked
  genuinely inert by symbol search, both turned out to be *deliberately retained compatibility
  surfaces pinned by their own tests*: `Paper2DPlus.Transitions.Deprecation.InputBufferProperties`
  asserts `InputBufferGraceFrames` "keeps its original reflected/config name", "remains
  config-loadable", and can still be populated by legacy reflected import, while
  `CrossSheetAlignment.SpriteOffset.PersistentProfileDataDoesNotMutateSharedPivot` asserts the
  `bHasCustomAlignment` marker "remains loadable under its original reflected name". A plain
  symbol-name search does not find these: UHT registers a `*_DEPRECATED` member under its BARE legacy
  name, so reflection lookups use `InputBufferGraceFrames`, not `InputBufferGraceFrames_DEPRECATED`.
  Anyone auditing this surface in future must search for the bare name too.

- **Hitbox Damage and Knockback are now floats (TASK-146, 2026-07-22) — BREAKING for Blueprint pins.** `FHitboxData`, `FWorldHitbox`, `FHitboxCollisionResult`, `FPaper2DPlusHitValidationResult`, `FPaper2DPlusAutoHitResult`, and the derived `FPaper2DPlusMoveFrameData.MaxDamage`/`MaxKnockback` all carry `float` Damage/Knockback (was `int32`), and the Blueprint library queries `GetFrameDamage`/`GetFrameKnockback`/`GetTotalDamage`/`GetMaxKnockback` now return `float`. **Saved assets are unaffected** — legacy int32 values auto-convert on load via tagged-property numeric conversion, and JSON import/export is number-typed either way. **Blueprint graphs need a touch-up**: pins wired from these struct members or query returns into integer inputs change type to float; refresh/reconnect the affected nodes (UE inserts the float→int conversion where you keep integer math). The Hitbox editor's Damage/Knockback fields accept decimals; frame-data CSV/JSON exports emit whole numbers unchanged and gain decimals only for fractional values. Layer bake digests hash the new representation, so pre-existing Fixed/Baked outputs report **Needs Bake** once after upgrading — re-bake to clear it (content is unchanged; the digest input format moved).

- **Character Layer schema v4 and exclusive delivery modes.** Stable `LayerId` plus editor-only `AuthoredAnimations` now own source art/placement/gameplay data independently from the canonical Character Profile output. `ECharacterLayerUsageMode` makes Legacy Runtime Layers, Canonical Bake, and Runtime Customizable mutually exclusive; migrated assets remain Legacy until explicitly adopted. Bake ownership/revision/manifest/recovery data and independent-live/recolor compatibility declarations are serialized with deterministic migration and validation. Bake compatibility tracks `CurrentManifestVersion=1` independently from `CurrentDigestVersion=4`: either future value makes the full set read-only, while either stale value requires one complete Bake All upgrade.
- **Executable Frame Events are load-only migration inventory.** The old classes, arrays, saved Blueprint signatures, and historical filenames remain hidden for one compatibility transition so native payloads can convert and unsupported custom graphs can be reported without data loss. Paper2DPlus runtime and editor preview never invoke `OnReceiveFrameEvent` or legacy Begin/Tick/End; new authoring uses data-only Frame Cues and the typed Cue listener.
- **Phase groups REMOVED entirely (2026-07 legacy cleanup) — BREAKING.** The whole-flipbook-as-slot phase-group system (`FPhaseGroup`/`FCustomPhaseSlot`, the asset's `PhaseGroups` array, the Startup/Active/Recovery slot cards, the "+ Phase Group" button, the Phase Group Details view, and the Frame Data table's "Group" column) is gone — it predated and clashed with the per-flipbook phase-tag + Animation Map system, which is now the ONE phase model (`EditorMeta.PhaseTag` under `Paper2DPlus.Phase.*`, chain-derived phases via Derive Phases, runtime inspection through the flipbook-keyed transition resolver). **Blueprint hard-break:** `GetFlipbooksInPhaseGroup` and the native phase-group query surface are deleted; `FPaper2DPlusMoveFrameData.PhaseGroupName` is removed (the frame-data CSV/JSON exports drop that column — a one-time column shift for consumers past `Phase`, whose value now derives from the flipbook's own PhaseTag). Existing assets migrate at load via `MigrateLegacyGrouping`: phase-group rows are dropped and their cards fall to Unassigned; the slot payload is discarded.
- **"Required tag mappings" REMOVED (2026-07 legacy cleanup) — BREAKING.** `UPaper2DPlusSettings::RequiredTagMappings` and the Blueprint query `GetUnmappedRequiredTags` are deleted. The old feature silently re-added its keys to every asset on each details refresh, resurrecting groups the user had deliberately removed. Group presence in the grid/list/Map views now derives purely from each asset's own non-empty `TagMappings` (an emptied/removed mapping vanishes; its animations land in Unassigned/Unmapped), and stale auto-created tag groups are pruned at load. `TagMappingDescriptions` (tooltips) stays.

- **Layered-asset terminology unified (2026-07-08) — BREAKING for C++ users, redirected for assets/Blueprints.** The Character Layer system's overloaded names were renamed across the data model, runtime API, and editor UI: stackable equipment **"Outfit" → "Wearable"** (`ECharacterLayerVariantKind::Wearable`, `DefaultWearableVariantIds`, `AddWearable`/`RemoveWearable`/`IsWearableActive`, `AddOrExtendWearableOption`, `MergeWearableData`, the Wardrobe's "Wearable Stack"); the presaved skins+wearables combination **"Look" → "Outfit"** (`FCharacterLayerOutfit`/`FCharacterLayerOutfitEntry`, `Outfits[]`, `DefaultOutfitName`, `SetOutfit`/`PreviewOutfit`/`GetOutfitNames` — the Wardrobe's "Saved Outfits" section is unchanged); and the additive overlay **"Effect" → "Cosmetic Effect"** (`ECharacterLayerVariantKind::CosmeticEffect`, `SetCosmeticEffectActive`) to distinguish it from the separate combat/VFX **Effect Profile** system, which keeps its name. Existing `.uasset`s and Blueprint graphs keep loading via the Core/Enum/Property/Function redirects shipped in `Config/DefaultPaper2DPlus.ini` (enum values serialize by name, so the old `Outfit`/`Effect` kind values are redirected too). C++ call sites must be updated to the new names.

- **`FFlipbookTagMapping` struct-ified (TASK-3 / NS-1):** the parallel `FlipbookNames` + `PaperZDSequences` arrays are replaced by a single `TArray<FFlipbookTagMappingEntry> Entries`, where each entry pairs a `FlipbookName` with its optional `PaperZDSequence`. A name and its sequence can no longer fall out of lockstep. Legacy assets migrate automatically at load (and on JSON import) via `MigrateTagMappingsToEntries`; the old arrays survive as `*_DEPRECATED` fields purely for that migration.
- **Historical v7 behavior, superseded by the v8 TASK-165 entry above — `FFlipbookEffectData` formally deprecated (TASK-3 / NS-1):** the struct became `UE_DEPRECATED`, and the `FFlipbookProfileEntry.Effects` field was renamed `Effects_DEPRECATED` with `meta=(DeprecatedProperty)`. At the time, legacy effects migrated idempotently to direct `UPaper2DPlusSpawnEffectCue` data at load; v8 deleted that conversion and deliberately leaves the retained rows populated and inert. The dead "Effects" summary row was removed from the Profile Details panel.

### Fixes

- **Standalone archives now carry the engine version that built them:** the guarded BuildPlugin path no longer passes `-Unversioned`, which could preserve the source 5.8 descriptor in UE 5.0–5.7 packages. AutomationTool stamps each packaged `.uplugin`; the wrapper derives the required `Major.Minor.0` from that engine's `Build.version` and rejects the result unless `expected_engine_version`, `packaged_engine_version`, and `engine_version_verified` prove an exact match alongside the existing release-content audit. Release archives must not be manually restamped after verification.
- **Slate lifetime, selection, and keyboard paths are hardened:** Catalog tag editing copies picker-owned values before closing its menu and defers the model mutation to the surviving panel; Group/member selection is restored by stable value rather than stale list-item pointers; visible card thumbnails use bounded recency and safe async completion/teardown; Effect frame seeking and Combat Lab positioning have focus-aware keyboard paths with visible/accessibility feedback.
- **Combat Lab now matches runtime frame geometry:** shared pivot-space conversion, fractional pivots, facing, scaling, root motion, clash defaults, and defense classes drive both runtime collision and the visual lab; selected-move edits reevaluate without reopening the editor.
- **Combat variable inheritance is sparse without silent migration:** new profiles store only tag/move/scenario overrides, while legacy dense snapshots remain intact until the designer runs the undoable **Review inheritance** action; that review removes only values exactly equal to their current inherited result.
- **Validation follows external dependencies and derived asset classes:** open panels coalesce refreshes when linked profiles, Catalog/Effect relationships, or relevant settings change, and the validation commandlet discovers native or Blueprint-derived Paper2DPlus asset classes through the Asset Registry hierarchy.
- **Runtime appearance profiling reports what it actually measures:** the release harness runs the real render-capable production component/subsystem/cache path and emits schema `3` with `TimingStatus=RenderCapableProductionPathMeasured`. Cold descriptor mutation, recipe creation, cache admission, construction, submission, and queue drain are recorded separately. Warm comparisons first establish the 140 Far / 50 Near / 10 Changing production distribution, then advance/update/capture all 200 real components without repeatedly reauthoring outfits inside the timed loop. Cache admission reservations, initialized render-target resource bytes, and persistent descriptor-serialization estimates are separate fields; descriptor size is no longer described as packet or wire traffic. A release pass requires all eight gates: `PrimitiveCapsPassed`, `WarmGameThreadImproved`, `WarmRenderThreadImproved`, `CacheBudgetPassed`, `CompositeWorkMillisecondsBudgetPassed`, `SynchronousLoadGatePassed`, `NetworkAuthorityAndPayloadContractPassed`, and `ProductionComponentSubsystemCachePathPassed`. Cache resource lookups are indexed and shared frame builds use a monotonic claim cursor instead of repeated linear scans.
- **Synchronous real-RHI proofs no longer misread older D3D11 generic fences:** through UE 5.5, `FGenericRHIGPUFence::Poll()` may wait for a later render-frame number even after a blocking GPU-idle command. Runtime remains nonblocking and fence-polled across real frames; only editor/test proofs may use the explicit post-GPU-idle completion seam after the wait and render-command flush have already established completion.
- **Canonical bake rejects source art it cannot reproduce exactly:** source textures must use the standard Paper2D Pixels2D/UI profile with explicit `TF_Nearest` and `TMGS_NoMipmaps`; inherited/default filter or mip behavior is not accepted. Empty/incomplete/non-manifold render geometry and outer edges through texel centers fail closed rather than silently changing coverage.
- **Gameplay-tag picker right-click crash fixed:** right-clicking a tag row inside any dropdown-hosted tag picker (flipbook-card tag chips, Animation Map "Change Group…", Clash Graph "+ Add Category…", and the colored FGameplayTag property chip) could fatal-assert the editor with "Window Creation Failed (error 1400)" — the engine picker's management context menu can't be safely pushed from inside an auto-dismissing menu host. Menu-hosted pickers are now selection-only (right-clicks are swallowed); tag management lives in the Gameplay Tag Manager window.
- **Character Profile editor no longer takes many seconds to close:** the editor module's 8 explicit `GConfig->Flush` calls (including one on EVERY Animation Map pan/zoom) each rewrote the whole `EditorPerProjectUserSettings.ini` and could stall ~8s apiece under file-lock contention. UI state is still persisted; the engine flushes the ini itself.
- **Legacy JSON import (TASK-51):** importing a Character Profile JSON with a non-empty legacy `Animations` array (and pre-sub-struct / pre-struct-ify fields) now populates the current data instead of silently dropping it.
- **Frame Cue timing across frame edits (evolved from TASK-61):** excluding, restoring, or reordering a frame remaps Cue anchors in lockstep, so Cues do not move to the wrong frame. Data on an excluded frame is stashed and reattached on restore; legacy Frame Event rows use this only as migration inventory.
- **Animation Map nodes no longer shuffle themselves:** the Map view's `ReapplyPositions` used to force every node back to its stored / `FirstLayout` auto-grid position on every reconcile — and since the TASK-96 Animations-tab merge makes the Map reconcile on every selection, any node not yet in the durable position map snapped back to the auto-grid the moment it was moved. It now re-asserts a stored position only on a fresh spawn or a genuine stored change (undo / external edit / committed drag — undo still works), and otherwise keeps the position the user dragged the node to.

### Editor

- **Flipbooks list panel restyled** as clean cards — row spacing, framed thumbnails, name + frame-count subtitle, and a selection accent bar (replaces the cramped near-black rows).
- **Editor icon standardization (2026-06-27 audit):** a central icon registry (`Paper2DPlusEditorIcons.h`) + a plugin Slate style set (`FPaper2DPlusEditorStyle`) with **custom bundled SVG icons** (clock / film-strip / bounds-box / image, under `Resources/Icons/`). Every editor tab now has a distinct, sensible glyph (was: duplicated/blank tab icons); the flipbook right-click menu gained icons + tooltips.
- **Designer visual tour expanded to 31 screens:** `scripts/run-paper2dplus-visual-tour.ps1` drives the 15 normal/core Character list/map/tools, Layer Workspace, Effect, Combat, and Catalog surfaces plus 16 narrow, wide, and empty-first-run pressure views through an external watchdog. It requires the exact 31/31 manifest and images, semantic success for every view, cleanup/preference restoration, and a zero editor exit. The same wrapper accepts a packaged clean-host project when development-only resident services interfere with unattended Slate.

### Major features since v6.2 (summarized; see CLAUDE.md for detail)

- **Action-data orchestration (TASK-76):** pure From→To transition/combo map, author-query curves, explicit hit-stop, and the **Animation Map** node-graph editor; playback/input/cancel decisions remain game-owned.
- **Curves rework:** one embedded multi-curve `SCurveEditor` under Frame Cues (dedicated Curves tab retired).
- **Combat Profile** (`UPaper2DPlusCombatProfileAsset` + advisory scoring + guided Setup/Attacks/Playground/Lab workspace) and **Effect Profile** (tagged visual library workspace).
- **Character Layer "Major Additions"** epic (Parts, Skins, stacked Wearables, Cosmetic Effects, saved Outfits, additive reimport, recolor, and composited thumbnails).
- **Flipbook Draw tool** (Aseprite-style pixel editing on flipbook frames).
- **Opt-in network replication (TASK-57):** server-authoritative anim-state / combat / hit-stop / wardrobe; single-player byte-identical.
- **Unified Animations tab (TASK-96):** merged Overview + Animation Map into one List/Map tab with a shared details pane.
- **2026-06-27 deep audit (historical evidence):** bug fixes (combat var-shadow, sprite-offset facing, `.ase` overflow hardening, Combat undo client, Animation Map graph centering, BP query resolution, …) + the icon work above repaired the then-current UE 5.0–5.8 build matrix and surfaced cross-version runtime fixes. The larger designer-overhaul diff was re-verified independently; its current results are summarized under Supported Engine Versions and must not inherit that old green result:
  - **F17 — Constant (step) curve eval:** `FPaper2DPlusFrameCurve::Eval` now evaluates step curves identically on every engine. UE ≤5.2's `FRichCurve` returned the *previous* key's value when a step curve was sampled exactly on a key, so cancel-window / hit-stop / hit-window timing diverged on 5.0–5.2 — fixed by evaluating Constant curves manually.
  - **F18 — 5.0-only guards:** explicit `= nullptr` on `FPaper2DPlusEffectSpawnSettings` flipbook members (UE 5.0 stack-USTRUCT null-init) + a `#if ENGINE >= 5.1` guard on a profile-duplicate test (a genuine 5.0 CoreUObject duplicator crash on flipbook-entries + node-positions together).
  - **`Paper2DPlus.VisualTour`** began as a seven-tab cross-version screenshot command. The current contract is the 31-screen repo wrapper described above; current overhaul evidence is 31/31 on UE 5.8, while runtime/profile and package proofs remain separate concerns.

### Breaking Changes

- **Blueprint API hard break — wardrobe, group-query, and delegate surfaces removed without deprecation.** No redirects ship for these deletions; affected Blueprint graphs become broken nodes on load and must be rewired manually.
  - `UPaper2DPlusLayerRenderComponent`: the old wearable swap API (`SetActiveVariant`, `SetLayerVisible`, `IsLayerVisible`, `SetSkin`, `AddWearable`, `RemoveWearable`, `IsWearableActive`, `GetActiveSkin`, `SetCosmeticEffectActive`, `SetOutfit`, `ResetPartToBase`, `PreviewPartVariant`, `PreviewOutfit`) is replaced by the new `Paper2D+|Appearance` surface. The three `BlueprintAssignable` frame-event delegates (`OnFrameEventFired`, `OnFlipbookChanged`, `OnFrameChanged`) are replaced by the single `OnFrameCue` delegate.
  - `UPaper2DPlusCharacterProfileAsset` group-query family: `GetFlipbookDataForTag`, `GetFlipbooksForTag`, `GetFirstFlipbookForTag`, `GetRandomFlipbookForTag`, `GetPaperZDSequenceForTag`, `GetTagMapping`, `HasTagMapping`, `GetFlipbookCountForTag`, `HasTransitions`, and the `BlueprintPure` transition queries are deleted. Use the Animation Map library (`Paper2DPlusAnimationMapLibrary`) instead.
  - `FFlipbookTagMapping` and `FFlipbookTagMappingEntry` lose `BlueprintType`; raw mapping entries are not a Blueprint-visible surface.
- **CoreRedirects removed — saved Blueprint nodes and serialized values using the old names no longer resolve.**
  - 5 group-query `FunctionRedirects` deleted (`GetAnimationsForGroup`, `GetFlipbooksForGroup`, `GetFirstFlipbookForGroup`, `GetRandomFlipbookForGroup`, `GetPaperZDSequenceForGroup`); also `GetGroupBinding`, `HasGroup`, `GetAllMappedGroups`, `GetAnimationCountForGroup` (4 more).
  - Entire 2026-07-08 layered-asset terminology block removed: `ECharacterLayerVariantKind` `ValueChanges` (`Outfit`→`Wearable`, `Effect`→`CosmeticEffect`), 4 property redirects (`DefaultOutfitVariantIds`, `OutfitVariantIds`, `ActiveEffectVariantIds`, and the rep-state field), 6 function redirects (`AddOutfit`, `RemoveOutfit`, `IsOutfitActive`, `SetLook`/`PreviewLook`/`GetLookNames`→Outfit, `SetEffectActive`→`SetCosmeticEffectActive`), and 4 struct/property redirects for `CharacterLayerLook`/`CharacterLayerLookEntry`/`Looks`/`DefaultLookName`. Assets serialized under the pre-rename names will not load correctly.
- **Character Layer schema v5 — pre-v5 wearable-era data discarded on load (TASK-129).** The wearable-era fields (`Parts`, `Outfits`, `Variants`, `DefaultOutfitName`, `FCharacterLayerPart`, `FCharacterLayerPartVariant`, `FCharacterLayerOutfit`, `FCharacterLayerOutfitEntry`, `ECharacterLayerVariantKind`, `ECharacterLayerPartRole`, `EPaper2DPlusLayerBoxMerge`, `FCharacterLayerAnimationOverride`, and related BP-callable APIs on `UPaper2DPlusCharacterLayerAsset`) were removed with no `_DEPRECATED` shells. Pre-v5 Layer assets load but lose all wearable-era data silently when saved. A new load-time `Warning` log names the asset and directs re-authoring in the Layer Workspace. There is no migration executor.
- **Custom Blueprint Frame Events are never dispatched at runtime.** The old `UPaper2DPlusFrameEvent` / `UPaper2DPlusFrameEventState` subclasses are now hidden load-only migration inventory (`Hidden, HideDropdown`); their `OnReceiveFrameEvent` / Begin / Tick / End signatures survive only so saved custom Blueprint graphs can be inventoried. The editor migration tool flags custom-graph placements as `Blocking`; the runtime emits a Warning naming the asset when a hidden legacy class is encountered. Manual receiver rewire to the Frame Cue + typed listener model is required.

### Fixes

- **Loop-seam Range Cues no longer End+Begin on every loop.** A Range Cue whose window spans the last frame(s) of a looping flipbook previously dispatched End on the loop wrap and Begin again at the next cycle entry, producing spurious lifecycle churn on perpetually looping moves. The latched-out set now clears at loop wrap so each range re-evaluates at its next Begin edge without forcing an End/Begin pair on animations that never leave range.
- **`Shaders/` now staged for Fab packaging.** `FilterPlugin.ini` /Shaders/... line added; `Paper2DPlusAppearanceComposite.usf` now ships in BuildPlugin archives.

### Supported Engine Versions

- **Target support: UE 5.0–5.8** (primary 5.8). Final-source evidence includes the 31/31 UE 5.8 designer tour, the D3D11 schema-v3 Profile200 matrix passing all eight gates on UE 5.0–5.8 (9/9), and zero-failure full-suite runs on the completed final-hash UE 5.0, 5.6, 5.7, and 5.8 legs. The broader full-suite invocation was intentionally stopped during UE 5.5, and no standalone/Fab or forced-no-PaperZD package was produced in this closeout; do not infer those unrun claims from the available evidence.

## v6.2 — 2026-04-29

### Supported Engine Versions

- Unreal Engine 5.0, 5.1, 5.2, 5.3, 5.4, 5.5, 5.6, 5.7

### New Features

**Bulk Sprite Extractor**
- Full multi-texture extraction pipeline — load multiple sprite sheets, detect frames, and extract in one pass
- Grid detection mode with configurable rows/columns for uniform sprite sheets
- Grid-aware auto-padding with ground plane detection
- Canvas with keyboard navigation and detection settings overlay
- Folder organizer with drag-and-drop, batch rename (prefix/suffix), and profile name preview
- Texture nesting — group extracted sprites by source texture
- Prefix merge across textures for consistent naming
- Auto-assign PaperZD sequences from extracted flipbook names
- Auto-create flipbook groups from folder organizer structure
- Pad confirmation step before committing extraction
- Folder persistence and profile picker across sessions

**Cross-Sheet Alignment**
- Uniform bounds computation across multiple sprite sheets
- Cross-texture alignment with three-phase padding and convergence guard
- Re-Align Flipbooks button on Overview tab for one-click realignment

**Frame Event System**
- First-class frame event subclass library — typed events dispatched per-frame during flipbook playback
- Frame-skip handling for events that span skipped frames
- Null root component safety for PlaySound and SpawnEffect events
- Diagnostic logging for frame event dispatch

**PaperZD Integration**
- Scan and auto-create PaperZD animation sequences for tag mappings
- Confirmation dialog before sequence creation
- Blueprint-exposed frame events (removed PaperZD editor module dependency)

**CharacterProfile Enhancements**
- World transform properties on CharacterProfile asset
- Asset thumbnail with user-picked override image
- Per-asset last-selection memory — editor remembers which flipbook/frame was last viewed
- Overview tab improvements: relative transform display, content browser drag-and-drop, DisplayName

**Validation Commandlet**
- CI-ready validation commandlet for pre-commit hooks and automated pipelines

### Improvements

- Per-tab help system with algorithm documentation
- File-level comments and improved tooltips across all source files
- Shared `SDragClickWrapper` widget replaces duplicate drag wrappers
- Sanitization logic consolidated to `FSpriteExtractionUtils`
- Major file splits for maintainability: SpriteExtractorWindow (3 files), FrameEventEditor (3 files), plus FlipbookGroups and SpriteEditor widget extractions

### Breaking Changes

- **Re-Align Flipbooks and Apply Uniform Bounds buttons removed** from the bulk extractor — uniform bounds are now applied automatically during extraction
- **`FFlipbookEffectData` deprecated** — use frame event subclasses instead

### Bug Fixes

- Fixed frame event dispatch skipping frames during rapid playback
- Fixed null root component crashes in PlaySound and SpawnEffect frame events
- Fixed combine textures alignment (top-align instead of bottom) with size guard
- Fixed CharacterProfile not propagating when set from GameMode
- Fixed playback queue wrap-around at boundaries
- Fixed hitbox editor playback ignoring sprite editor queue
- Fixed canvas GC rooting (TStrongObjectPtr → FGCObject) preventing potential crashes
- Fixed empty prefix guard in sprite/texture name formatting
- Fixed stable framing during playback queue transitions
- Fixed add-flipbook replacement bug, phase slot picker, and scroll performance
- Fixed editor-only `GetSourceTexture()` API usage without `WITH_EDITORONLY_DATA` guard

### Cross-Version Compatibility

- Extended support from UE 5.5–5.7 down to UE 5.0–5.7
- Version guards for `GetClassPathName`, `GetObjectPathString`, `SGameplayTagCombo`, `EditorStyle` module, and other APIs unavailable in older engine versions
- `UCLASS` macros moved outside preprocessor blocks for UE 5.0 UHT compatibility

---

## v6.0 — 2026-03-14

### Supported Engine Versions

- Unreal Engine 5.5, 5.6, 5.7

### Breaking Changes

- **CharacterData → CharacterProfile rename** — all classes, structs, enums, functions, properties, files, and display strings renamed from "CharacterData" to "CharacterProfile" across the entire plugin (37 source files, ~700 occurrences)
  - `UPaper2DPlusCharacterDataAsset` → `UPaper2DPlusCharacterProfileAsset`
  - `UPaper2DPlusCharacterProfileComponent` → `UPaper2DPlusCharacterProfileComponent`
  - `SetActorCharacterData()` → `SetActorCharacterProfile()`
  - All header files renamed accordingly
  - CoreRedirects provided for seamless asset migration — existing `.uasset` files load automatically with no manual steps
  - Redirect chains collapsed for legacy names (e.g., `BlueprintHitbox` → `CharacterData` → `CharacterProfile` compressed to direct redirects)
  - C++ consumers must update `#include` paths and symbol references
- **CoreRedirects moved** from `DefaultEngine.ini` to plugin-local `Config/DefaultPaper2DPlus.ini` — ships with the plugin for Fab distribution portability
- Plugin version bumped to 6.0 (major) due to public API header and function signature changes

### New Features

**Frame Multi-Select and Batch Operations Overhaul**
- Ctrl+click toggle and Shift+click range select for frames across all three editor tabs (Hitbox, Alignment, Frame Timing)
- All batch operations gain "Apply to Selected" variants via a shared `ForEachSelectedFrame` helper
- Hitbox editor: batch damage/knockback moved into the Properties panel, "Copy to Remaining" replaces old Copy button, Mirror/Clamp buttons removed
- Alignment editor: "Apply to Selected" for alignment offsets, sprite flip section and reticle checkbox removed, `ESpriteAnchor::None` added to anchor dropdown
- Frame Timing editor: batch tools moved from toolbar to a dedicated right-side panel with descriptive labels, multi-select support on duration list rows
- Arrow keys clear multi-select to prevent confusion between single-frame and batch context

**Tag Mappings Panel Redesign**
- Required tags auto-populated from project settings — no manual setup needed
- Alphabetical card layout with drag-and-drop flipbook assignment onto tag cards
- Per-flipbook PaperZD animation sequence pickers — assign PaperZD sequences directly alongside flipbook-to-tag bindings via a parallel `PaperZDSequences` array
- Visual card design with two-row layout: tag name + metadata on top, assigned flipbooks below

**Non-Destructive Frame Exclusion and Restore**
- Exclude individual frames from a flipbook without deleting them — excluded frames are hidden from playback and hitbox editing but preserved in the asset
- Restore excluded frames at any time with their original data intact
- Reference frame identity preserved after exclusion — frame indices don't shift
- Exclude/restore actions available via context menu and alignment editor overlays

**Asset Validation System**
- `UPaper2DPlusCharacterProfileAssetValidator` integrates with UE's DataValidation subsystem
- Validates CharacterProfile assets on save: checks for duplicate flipbook names, empty hitbox frames, missing flipbook references, orphaned extraction data
- Validation issues surfaced with severity levels (Error, Warning, Info) in the standard UE validation UI

**Hitbox Bounds Clamping**
- `ClampHitboxToBounds` ensures hitboxes stay within sprite dimensions
- Prevents hitboxes from extending beyond the source sprite area during editing

**Horizontal Frame Strips**
- Compact horizontal frame strip with sprite thumbnails in the Hitbox and Alignment editors
- Click to select frames, visual highlight on current frame
- Excluded frames shown with overlay indicators in the Alignment tab

**Inline Rename**
- Double-click flipbook names in the Overview tab to rename inline
- Inline rename for flipbook group headers

**Context Menus**
- Right-click context menus on flipbook cards, frames, and group headers throughout the editor
- Frame-level context menus for exclude/restore, copy hitbox data, and navigation

**Frame Drag-and-Drop Reorder**
- Drag frames to reorder within a flipbook in the Alignment editor
- Visual drop indicators show insertion position

**Persistent Splitter Layout**
- Editor panel splitter positions saved to `GEditorPerProjectIni` and restored across sessions
- Layout version bumped (`_v1` → `_v2`) for the CharacterProfile rename

**Extractor Output Folder Picker**
- Sprite extractor now has a folder picker for choosing the output directory
- Output folder defaults to a sensible path based on the source texture name

**Non-Uniform Scale Debug Rendering**
- `DrawDebugHitboxes`, `DrawDebugHitbox`, and `SocketToWorldSpace` now accept separate X and Y scale values
- Debug visualization matches actual collision results when `ScaleX != ScaleY`

### Improvements

- "Edit" tool renamed to "Hitboxes" in the hitbox editor toolbar for clarity
- Close dialog prevents double-prompt on editor close (engine calls `OnRequestClose` twice)
- GEditor null safety checks added across HitboxEditor, FrameTimingEditor, and CharacterProfileAssetEditor
- `SFlipbookThumbnail` initial resolve timer for smoother thumbnail loading
- Toolbar wrapping via `SWrapBox` prevents buttons from clipping off-screen in narrow windows

### Bug Fixes

- Fixed reference frame identity shifting after frame exclusion operations
- Fixed double unapplied-offset dialog when closing the editor
- Fixed stale "CharacterData" references in comments, tooltips, test messages, and UI strings
- Fixed `SetActorCharacterProfile` Blueprint category (`Collision` → `Setup`)
- Fixed tag mapping card backgrounds and long tag name wrapping
- Fixed null safety issues in undo/redo paths when GEditor is unavailable
- Fixed `GetActiveTopLevelWindow` null checks in toolkit close dialog

---

## v5.1 — 2026-03-05

### New Features

**Flipbook Groups (Overview Tab)**
- Collapsible visual groups with customizable names and colors
- Nested groups — groups can have parent groups for hierarchical organization
- Drag-and-drop flipbooks between groups, reorder within groups
- Multi-select with Ctrl+click and Shift+click
- Auto-group by prefix — automatically create groups from flipbook name prefixes
- Inline rename — double-click group headers to rename
- Search filtering across all groups
- Group assignments persist on the Character Profile Asset

**Dockable Tab Editor**
- Character profile editor now opens as a dockable tab within the UE editor (FAssetEditorToolkit)
- Single-instance behavior — re-opening the same asset focuses the existing tab
- Warns about unapplied alignment offsets on close

**World-Space Hitbox API**
- New actor-based Blueprint functions: `GetActorHitboxes`, `GetActorAttackBoxes`, `GetActorHurtboxes`, `GetActorCollisionBoxes`, `GetActorSockets`, `GetActorSocketByName`
- Returns `FWorldHitbox` / `FWorldSocket` with pre-computed world-space bounds
- Auto-resolves position, flip, and scale from `UPaper2DPlusCharacterProfileComponent` and `UPaperFlipbookComponent`

**Ping-Pong Playback (Alignment Editor)**
- Forward + reverse playback mode
- Forward onion skin for previewing upcoming frames

**Universal Arrow Keys**
- Arrow key navigation works across all editor tabs (Overview, Hitbox, Alignment, Timing)
- Arrow keys wrap across flipbook boundaries — right on last frame advances to next flipbook's first frame
- Navigation follows queue order when a playback queue is active

**Unapplied Offset Indicators (Alignment Editor)**
- Visual indicators show when alignment offsets have been changed but not yet saved

**Grouped Flipbook Lists**
- Collapsible group headers in all editor tabs (Overview, Hitbox, Alignment)

**Multi-Flipbook Playback Queue (Alignment Editor)**
- Drag flipbooks from the sidebar into a playback queue to preview transitions
- Reorder queue entries via drag-and-drop or right-click context menu (Move Up / Move Down / Remove)
- Time-based playback respects per-frame durations across queued flipbooks
- Right-click any flipbook in the sidebar to add it to the queue
- Queue entries validated on undo/redo — invalid entries automatically purged

**Cross-Animation Onion Skin**
- When viewing frame 0, onion skin continues into the previous flipbook's trailing frames
- Previous flipbook frames tinted purple to distinguish from same-flipbook onion frames
- Uses queue order when active, list order otherwise

**Search Bar (Alignment Editor)**
- Filter the flipbook list with a debounced search input

### Refactors

- **Animation → Flipbook rename** — all "Animation/Animations" terminology renamed to "Flipbook/Flipbooks" throughout codebase (structs, functions, UI labels)
- **Group Mappings → Tag Mappings** — `FAnimationGroupBinding` renamed to `FFlipbookTagMapping`, panel and file renamed accordingly
- **Button style unification** — all editor buttons use `FlatButton.Default` convention
- **Hitbox editor cleanup** — batch operations panel and undo history panel removed
- **Alignment toolbar cleanup** — flipbook name removed from toolbar
- **Overview tab redesign** — add/remove moved to toolbar, animated flipbook cards with picker

### Bug Fixes

**Editor**
- FPS-aware frame duration labels and colors — classify by real hold time (ms), not raw frame count
- Checkerboard transparency grid behind sprite/flipbook thumbnails
- Ctrl+Shift+Z now correctly triggers redo instead of undo
- Root groups prevented from appearing inside Ungrouped section
- Flipbook card polish and group collapse fixes in alignment tab
- Queue name staleness, external asset refresh, no-queue frame wrapping
- Playback queue zoom stabilization, lighter card backgrounds
- Alignment grid anchored to reticle position with improved visibility
- Hitbox transform derived from flipbook component
- CoreRedirect targets corrected for renamed functions
- Queue Move Down no-op and end-of-queue drop target fixes

**Sprite Extractor**
- Toolbar wrapping to prevent off-screen clipping
- Prefixless name in extractor output
- Fixed _Texture suffix stacking on re-extraction
- Fixed auto-close behavior on extraction cancel

---

## v5.0 — 2026-02-28

Initial release.
