# Changelog

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
