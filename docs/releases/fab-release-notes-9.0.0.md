# Paper2DPlus 9.0.0 — release notes

**2026-09-04**

**Headline: directional animation sets.** Character Profiles can now own an optional
multidirectional Directional Animation Set per logical animation (TASK-190) — a base flipbook
still holds identity, hitboxes, root motion, curves, transitions, tags, and Frame Cues, and a
sparse, presence-aware set of facing-specific variants sits beside it. Profiles default to eight
evenly spaced slots at zero degrees; designers can choose 3–16 slots and -45 through 45 degrees
globally or per animation. One accessible radial wheel, shared by Animations, Hitbox, Sprite,
Frame Timing, Frame Cues, and Root Motion, authors and previews the set with click or remappable
hold-point-release selection and a persistent slot inspector. Cooked Blueprint gains **Has Multi
Direction**, **Resolve Directional Flipbook**, and **Get Occupied Direction Slots**; resolution
matches PaperZD 2.2.4's +Y-zero clockwise sector math and fails explicitly for an exact empty
sector — there is no nearest/base fallback once a set is populated. The feature works without
PaperZD; projects remain responsible for mapping the returned flipbook to a PaperZD sequence and
for playback.

Around that sits an Aseprite import overhaul: the modal `.ase` import dialog is gone, `.ase`
files are now batch sources inside the Bulk Sprite Extractor, reimport diffs structurally
instead of guessing from names, and a save writes only what actually changed. Several breaking
header/class removals ride along with it. Read the breaking changes below before opening an
existing project with 9.0.0.

## Breaking changes — read before opening your project

### Directional animation (TASK-190)

- **Character Profiles gain a Directional Animation Set, migrating Character Profile JSON from
  schema 8 to 9.** Existing Profiles migrate automatically as base-only data on load; existing
  proxy-animation replication stays base-only. **What to do:** open and resave your Character
  Profile assets on 9.0.0 so the migration commits to disk; any tooling or scripts that parse or
  produce Character Profile JSON must be updated for schema 9's directional fields.
- **`Get Occupied Direction Slots` is now BlueprintCallable (impure).** It synchronously loads
  every occupied directional variant, so the cost is now an execution step instead of hiding
  behind a pure node the K2 compiler may fold. **What to do:** wire it into an exec chain; use
  the new pure **Get Occupied Direction Slot Indices** where you only need a load-free occupancy
  check.
- **The direction wheel and the Character Profile header chrome were reworked.** The wheel is
  repainted onto the editor's theme (theme-token plate, selection-green wedges, compass labels,
  a focus-visible ring, and a DPI-correct centering fix for the held-shortcut wheel), and the
  Direction/inspector/Assign/Clear cluster now appears only on animations that already carry a
  Directional Set — other animations show a single **Add Directions…** entry point instead. This
  is a visual and workflow change, not an API break. **What to do:** nothing — no asset or code
  changes are required, but expect the header and wheel to look and behave differently on open.

### Aseprite import and Layer Workspace

- **The `.ase` import modal dialog is gone.** `.ase`/`.aseprite` files are now BATCH sources in
  the Bulk Sprite Extractor: dropping a file (or **Paper2D+ Actions → Import Aseprite Files…**)
  loads it as a source row beside any textures, where one Character Profile and one Layer
  Profile are chosen for the whole batch and a single **Extract All** imports them together —
  every file in the batch merges into that one Layer Profile instead of minting its own
  `<prefix>_Layers`. `FAsepriteImporter::ShowImportDialog` no longer opens a window with a typed
  output path/prefix that calls `ImportFile` directly; it now prompts for one or more files and
  loads them into the Bulk Sprite Extractor. `FAsepriteImporter::ImportFile` itself is unchanged
  and still available to C++ callers. **What to do:** nothing for existing assets. Dropping several
  `.ase` files on the Content Browser now loads all of them (a drag-and-drop extender claims the
  drop before the engine's one-file import loop); only the **Import** button / File > Import path
  keeps the engine's one-file limit, so use a drop or **Import Aseprite Files…** for a whole set.
- **`AsepriteLayerImportDialog.h` and `SAsepiteLayerImportDialog` are deleted.** The authoring
  surface moved to the Bulk Sprite Extractor's per-row **Edit…** window as `SAseRowImportEditor`
  (`AseRowImportEditor.h`); its composited preview widget moved out intact as
  `SLayerImportPreviewCanvas` (`LayerImportPreviewCanvas.h`). `FPerLayerBufferMap`,
  `EAsepriteImportMode`, and `FAsepriteLayerImportSettings` moved to `AsepriteImporter.h`, beside
  the importer that consumes them (`FAsepriteImporter::CompositePerLayer` now spells its return
  type `FPerLayerBufferMap`). `SAsepiteLayerImportDialog::InitDefaultSelection` is gone — call
  `FAsepriteImporter::InitDefaultSelection` instead. **What to do:** nothing for assets. C++ code
  that included `AsepriteLayerImportDialog.h` for the settings types should include
  `AsepriteImporter.h`; code that constructed the dialog has no replacement, because the import
  UI is no longer modal.
- **`LayerAuthoringWorkspace.h` is deleted**, along with `SLayerAuthoringWorkspace`,
  `FLayerWorkspaceResponsiveState`, and `ELayerAuthoringMode`. That widget was the Layer editor's
  embedded mode-switch surface before the docked-tab layout, and had been unreachable from
  production ever since `FCharacterLayerAssetEditorToolkit` started spawning Art, Hitboxes,
  Frame Cues, and Appearance as real tabs beside a persistent Structure dock. **What to do:**
  nothing for assets or any supported workflow — the widget could not be opened. C++ that
  included the header for `ELayerAuthoringMode` has no replacement enum; tab activation is the
  mode switch now.

## What's new

### Directional animation

- Per-slot **Mirror** flag: each occupied slot row in Details > Directional Animation gains a
  Mirror checkbox, so an 8-way set can ship five authored facings and reuse three mirrored (the
  wheel labels a mirrored slot "(mirrored)"). `Resolve Directional Flipbook` and `Get Occupied
  Direction Slots` return the flag beside the art; applying the horizontal flip stays
  project-owned (typically actor scale).
- **Per-slot directional rows** in Details > Directional Animation — one details-style row per
  active slot (compass label, bearing, a standard soft-asset picker with drag-and-drop and
  clear), so a Directional Set is visible and editable as a list, not only through the wheel.
- **Auto-fill from names…** fills every empty active slot from flipbooks named
  `<Base>_<N/NE/E/...>` or `<Base>_<slot index>` in the base flipbook's folder, with a reviewable
  proposal and one undoable transaction; ambiguous matches are skipped and occupied slots are
  never overwritten.
- New cooked Blueprint nodes: **Get Direction Settings** (pure; effective count/offset/presence),
  **Get Occupied Direction Slot Indices** (pure; load-free occupancy), **Resolve Direction Slot
  Index** (pure; the resolver's exact angular math for any topology), and **Make Direction From
  Bearing** (pure).
- The Character Profile Component async-preloads the current animation's occupied directional
  variants when its animation cache warms, so a facing change during play no longer stalls on a
  synchronous load; dedicated servers skip the warm.
- `Resolve Directional Flipbook`'s `Direction Unoccupied` failure now reports the resolved empty
  sector in its Slot Index output (previously cleared to -1), so a Blueprint can implement its
  own nearest/base/hold fallback — the resolver itself still never falls back.
- Directional silhouette differences (trim rectangle, render-bounds) are now advisory Warnings
  instead of blocking Errors; the compatibility gate and validator block only on the
  trim-invariant frame space that keeps base-owned hitboxes, sockets, Cues, and root motion
  spatially correct — which had previously rejected the Bulk Extractor's own trimmed output.
- Angle Offset tooltips now state the actual sign convention (a positive offset buckets the
  incoming facing as more clockwise, rotating the sector layout counter-clockwise on screen) —
  sign-compatible with PaperZD's Directional Angle Offset.
- Details rows relabeled **Direction Count** / **Angle Offset** under an explicit "Profile
  defaults" hint; the presence row reads "N of M directions assigned".

### Aseprite import & Bulk Sprite Extractor

- **Structural reimport diffing.** A renamed layer or animation tag with unchanged pixels
  rebinds in place instead of appending a duplicate and stranding the old one — the layer or
  animation keeps its stable id, preset membership, exclusive group, placement, and every bit of
  authored gameplay data, and the generated sheet/sprites/flipbook rename through the standard
  redirector. Acceptance requires an unambiguous match, identical stamped content hashes, no
  sibling `.ase` contributing that name, and the new name not already owned — anything else
  degrades to delete-plus-add and is reported, never guessed. Authored data on a removed
  layer/animation is kept and reported rather than silently dropped; a walkable **Aseprite
  Import** Message Log page and editor notification name every decision.
- **Incremental reimport: a save regenerates only what actually changed.** Every generated asset
  class carries its own write gate (per-layer sheet pixel hashes, paired normal-sheet stamps, a
  whole-composite hash, flipbook structure hashes, direct profile-row comparison), checked
  against Asset Registry existence so a deleted generated asset is re-created rather than
  skipped. A pixel edit confined to one layer rewrites only that layer's sheet plus the
  composited sheet; a pure retime rewrites exactly the affected flipbook; a byte-level no-op
  dirties exactly one package (the restamped Layer Profile). **Force Full Reimport** on the Layer
  Profile's Content Browser menu bypasses every gate as the explicit recovery path, and works
  even while Live Auto-Reimport is off. Every reimport logs a one-line cost report (per-class
  written/skipped counts, packages dirtied, timing split).
- **Live .ase Auto-Reimport can be turned off** (Project Settings > Plugins > Paper2DPlus >
  Aseprite Import; default on). While off, changes are detected but deliberately not
  reimported; turning it back on reconciles by content hash so anything that drifted while it
  was off catches up without an editor restart.
- **The Aseprite batch is configured in one place.** The right-pane **ASEPRITE IMPORT** section
  under the Character Profile picker replaces the old full-width `.ASE BATCH` bar, adds a
  browsable **Output folder** (previously reachable only through Organize Folders), and gives
  both the Character Profile and Layer Profile pickers a **New…** popup that names the
  destination live, offers **Select** when it already exists, and never opens the engine's
  Save-Asset-As modal underneath the tool window. Texture-only sections collapse for an
  `.ase`-only batch, and the window relabels itself (**Import Aseprite Files**, **ASEPRITE
  FILES**, **Import All**) for a batch of files.
- **An import can propose its own Sections.** The per-row **Edit…** window's **Preview Section
  Suggestions…** derives one Section per top-level Aseprite folder (one level only — a layer at
  the file's root stays ungrouped), lets you tick, rename, or remove each proposed Section
  before anything is written, merges into an existing Section of the same name, and applies the
  `LayerGroups` row plus every member's `GroupId` in one transaction. Reimport never re-derives
  Sections — a curated assignment survives and a renamed layer carries its Section with it.
- Generated sprites keep their `PixelsPerUnrealUnit` across reimports (assigned only on first
  creation); skipped-but-resident sheets get drifted pixel-art import settings (filter, mip,
  compression, sRGB, streaming, LOD group) repaired on every pass.
- Carried forward from the retired modal into the batch/per-row workflow: per-tag import
  checkboxes with a scrubber that scopes to the clicked tag's frame range; configurable
  hitbox-layer-name prefixes (Project Settings → Paper2DPlus → Aseprite Import; defaults keep
  `attack*`/`hurtbox*` and add `hitbox*`); **"Keep .ase in project"** (default on, copies the
  source `.ase` project-relative into `SourceArt/`); picking an existing Character Profile
  imports additively INTO it; artist edits propagate even while the editor is closed (Layer
  assets carry source path + content hash as registry tags, a startup reconcile catches drift,
  and the watcher reacts to file ADD events too); generated assets organize into `Flipbooks/`,
  `Sheets/`, and `Sprites/<name>/` subfolders; and both picker rows gained a **New…** button.
- The recommended gameplay-tag taxonomy now ships with the plugin as a tag-ini source
  (`Config/Tags/Paper2DPlusTags.ini`): the `Paper2DPlus.Animation.*` dimensions and the
  `Paper2DPlus.Phase.*` model appear in every consuming project's tag picker with no per-project
  config copy, plus seven new universal tags (`Locomotion.Dash`, `Locomotion.WallJump`,
  `Locomotion.Hang`, `Context.Injured`, `Reaction.Grabbed`, `Reaction.BlockHit`,
  `Flavor.Defeat`).

### Animation Map

- No new Animation Map features land in 9.0.0. The one Animation Map change this release is a
  stability fix — an undo/redo crash when the mouse crossed a transition pill — listed under
  Fixes below.

### Runtime customization / appearance library

- **New Blueprint library `UPaper2DPlusAppearanceLibrary`** (category **Paper2D+ |
  Customization**) for character-creator UI. **Get Layer Options** / **Get Appearance Slots** /
  **Get Appearance Preset Options** describe a Layer asset (id, name, Exclusive Group, active
  state, paint order) before any actor exists; **Describe Layers** / **Describe Appearance
  Slots** / **Describe Appearance Preset Options** do the same for a live component; **Cycle
  Appearance Slot (By Name)** steps an Exclusive Group forward or backward with an optional
  wear-nothing position, **Select Slot Option** picks one directly, **Toggle Layer (By Name)**
  flips an independent Layer, and **Cycle Appearance Preset** walks the authored presets. Every
  mutation routes through the component's existing committed setters, so authority gating,
  normalization, gameplay recomposition, and replication are unchanged.

### Editor & validation

- **Layers can be dragged in the Layer Workspace's Structure dock.** Dropping onto a Section
  header changes membership only (the global order stays untouched); dropping into the gap
  between two rows moves a layer to that exact global position, shown by an insertion marker; a
  drop crossing a Section boundary applies the reorder and the new Section as one undo step;
  dropping onto **Ungrouped** removes a layer's Section. A drop that would change nothing (back
  into its own gap, onto its own Section, onto itself) is refused outright and opens no
  transaction.
- Directional silhouette validation now warns instead of blocking (see Directional animation
  above) — this had previously rejected the Bulk Extractor's own trimmed output.

### Cross-version

- No engine-support changes this release — Paper2DPlus continues to build across UE 5.0–5.8 (see
  Compatibility below). The one call-site fix that mattered most on the pre-5.6 range — the
  per-frame directional resolver no longer rebuilding a soft object path per candidate reference
  before its pointer compare — is listed under Fixes.

## Fixes

- Animation Map: fixed a crash (access violation in `SGraphPin::OnMouseEnter`) when the mouse
  crossed a transition pill after undoing or redoing a wire.
- Dropping several `.ase`/`.aseprite` files on the Content Browser now loads all of them, not
  just the first.
- An `.ase` layer whose name carries an engine-forbidden character (`& ! ~ @ # . ,`, quotes,
  brackets) now imports instead of silently producing no sheet or sprites.
- A dropped Aseprite batch now selects its first row instead of sitting on "No texture selected".
- Pointing the direction wheel at an empty/loading/unavailable sector no longer rescales
  base-owned hitboxes, sockets, and offsets.
- The Frame Cues frame strip now follows the directional preview like the canvas above it.
- Root Motion keeps its grid, ground line, motion path, handles, and frame strip when the
  bearing points at an empty slot; the Sprite and Hitbox tool titles now explain an empty
  bearing too.
- A blocked directional-count reduction is now actionable: every stranded-assignment row carries
  a **Clear slot** action beside Focus.
- The per-frame directional resolver no longer rebuilds a soft object path per candidate
  reference before its pointer compare (measurable per-call cost on UE 5.0–5.5).
- An `.ase` import now saves what it generates, in two passes (art first, then the Profiles that
  reference it); a save failure keeps the window open and names the packages that failed.
- The Bulk Sprite Extractor's GRID, DETECTION, and OPTIONS sections collapse while an `.ase` row
  is selected, since that row reads none of them.
- The live-reimport watcher never actually started — its `if (GEditor)` init guard ran while
  GEditor was still null; it now initializes on `OnFEngineLoopInitComplete`.
- Linked ("hold") cels no longer import as blank frames.
- The import no longer buries every asset in a folder named after the source file.
- Engine dialogs (e.g. Save Asset As) now stack above the import window instead of underneath it.
- "Keep .ase in project" now copies to `SourceArt/` instead of `Content/`, avoiding a loop with
  Unreal's own auto-import monitor.
- The watcher ignores its own echoes — a change event matching an asset's last-import hash no
  longer re-triggers the import that just produced it.

## Known issues

- **Force Full Reimport on a multi-source Layer Profile refuses shared layers.** A layer name present in two `.ase` files of one Layer Profile is one row whose sheet holds both files' frames. Replaying a single source cannot rebuild that row correctly, so the replay is refused with a message naming the shared layers; re-import every source together through the Bulk Sprite Extractor (**Paper2D+ Actions → Import Aseprite Files…**) instead. A per-source replay that rebuilds shared rows from every recorded source is planned for the next release.
- **Data validation of a plugin asset reports each appearance issue twice** (two validation codes for the same message) in the Message Log and the validation panel. Cosmetic; every issue is still correct.

## Compatibility

- Supported engines: Unreal Engine 5.0 through 5.8, one package per engine version.
  `EngineVersion` in `Paper2DPlus.uplugin` pins a single build for Fab upload purposes only; the
  supported range is owned by `README.md`/`ONBOARDING.md` per `RELEASING.md`.
- Paper2DPlus follows semantic versioning; 9.0.0 is a major with the breaks listed above and no
  others intended. The full, precise record — including every removed symbol — is in the
  plugin's CHANGELOG.md.
- Release gate ladder (Gate 1 build, Gate 2 packaging, Gate 3 cross-version test matrix, Gate 4a
  visual tour): Ladder run `20260904-194535` on dev `c01fd8b` (tag `paper2dplus-v9.0.0`): Gate 1 editor+game `-WarningsAsErrors` **pass**; Gate 2 nine guarded BuildPlugin packages **pass** (one source fingerprint `234EF51F…C57D7` across all nine); PaperZD-absent proof **pass**; Gate 3 host-runtime matrix **fail** — but only because the optional UE 5.0 cooked-runtime proof could not cook: the shared host project carried a stale scratch fixture (`Content/Paper2DPlusValidation/DA_ValidationCatalog.uasset`, saved by UE 5.8 on 2026-08-23 by the validation-fixture harness, a package format UE 5.0 cannot read), so BuildCookRun exited 25 before reaching the plugin's fixture. Every engine's automation suite passed with zero failures and zero silent-skip markers (the six UE 5.0 failures of the 2026-08-28 run are fixed), and the UE 5.8 cooked-runtime proof passed. The stale fixture is removed from the host; a single-version UE 5.0 rerun with the cooked proof is recorded below when it completes; Gate 4a visual tour **not-run** in the ladder (the chain stops at a failed gate); the standalone tour on the same tree passed 32/32 captured and 32/32 semantic (run `73cc69c75c9849f89aee5f08d486314b`, 2026-09-04); validation gate skipped (`-SkipValidation`, the project's `NCBJ_Test` null Cue slot is a host-content defect); Profile200 not requested. Overall **fail**.

## Upgrading from 8.0.0

1. **Resave your Character Profile assets after upgrading.** The Directional Animation Set
   migration (Character Profile JSON schema 8 → 9) runs automatically on load as base-only data;
   a resave commits it. Existing proxy-animation replication stays base-only until you author
   Directional Sets.
2. **Rewire any Blueprint graph using `Get Occupied Direction Slots` as a pure node** into an
   exec chain (it is now BlueprintCallable/impure); use the new pure **Get Occupied Direction
   Slot Indices** for a load-free occupancy check instead.
3. **Update C++ includes that named the two deleted headers.** `AsepriteLayerImportDialog.h` →
   `AsepriteImporter.h` for `FPerLayerBufferMap`/`EAsepriteImportMode`/
   `FAsepriteLayerImportSettings`, and `SAsepiteLayerImportDialog::InitDefaultSelection` →
   `FAsepriteImporter::InitDefaultSelection`. `LayerAuthoringWorkspace.h` /
   `ELayerAuthoringMode` have no replacement — nothing used them in production.
4. **Expect a different Aseprite import workflow.** Dropping a `.ase` file (or **Paper2D+
   Actions → Import Aseprite Files…**) now opens the Bulk Sprite Extractor instead of a modal
   dialog; a multi-file Content Browser drop, a drop onto the open window, or **Import Aseprite
   Files…** loads a whole set at once (only the Import button / File > Import path keeps the
   engine's one-file limit).
5. **Expect the direction wheel and Character Profile header to look different**, even on
   Profiles with no Directional Set — the header now shows a single **Add Directions…** entry
   point instead of the full Direction/inspector/Assign/Clear cluster.
