# Changelog

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
- Group assignments persist on the Character Data Asset

**Dockable Tab Editor**
- Character data editor now opens as a dockable tab within the UE editor (FAssetEditorToolkit)
- Single-instance behavior — re-opening the same asset focuses the existing tab
- Warns about unapplied alignment offsets on close

**World-Space Hitbox API**
- New actor-based Blueprint functions: `GetActorHitboxes`, `GetActorAttackBoxes`, `GetActorHurtboxes`, `GetActorCollisionBoxes`, `GetActorSockets`, `GetActorSocketByName`
- Returns `FWorldHitbox` / `FWorldSocket` with pre-computed world-space bounds
- Auto-resolves position, flip, and scale from `UPaper2DPlusCharacterDataComponent` and `UPaperFlipbookComponent`

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
