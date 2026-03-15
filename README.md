# Paper2D Plus

A character sprite data pipeline and visual editor for **Unreal Engine 5** Paper2D projects.

Paper2D Plus manages a 2D character's entire sprite data lifecycle -- from raw sprite sheet to combat-ready hitboxes -- through a single **Character Profile Asset**. Each asset bundles flipbooks, per-frame hitboxes/hurtboxes, sockets, sprite alignment, frame timing, visual groups, and tag mappings in one place.

## Supported Engine Versions

- Unreal Engine 5.5, 5.6, 5.7

## Table of Contents

- [Installation](#installation)
- [Quick Start](#quick-start)
- [Sprite Extraction](#sprite-extraction)
- [Character Profile Asset Editor](#character-profile-asset-editor)
  - [Overview Tab](#overview-tab)
  - [Hitbox Editor Tab](#hitbox-editor-tab)
  - [Alignment Editor Tab](#alignment-editor-tab)
  - [Frame Timing Tab](#frame-timing-tab)
- [Flipbook Groups](#flipbook-groups)
- [Flipbook Tag Mappings](#flipbook-tag-mappings)
- [Frame Exclusion](#frame-exclusion)
- [Asset Validation](#asset-validation)
- [Runtime Components](#runtime-components)
  - [Character Profile Component](#character-profile-component)
  - [Debug Component](#debug-component)
- [Blueprint API Reference](#blueprint-api-reference)
  - [Collision Detection](#collision-detection)
  - [World-Space Hitbox API](#world-space-hitbox-api)
  - [Frame Data Helpers](#frame-data-helpers)
  - [World Space Conversion](#world-space-conversion)
  - [Tag Mapping Lookups](#tag-mapping-lookups)
  - [Attack Bounds (AI Helpers)](#attack-bounds-ai-helpers)
  - [Debug Visualization](#debug-visualization)
  - [Serialization](#serialization)
- [Project Settings](#project-settings)
- [Aseprite Import](#aseprite-import)
- [Data Types Reference](#data-types-reference)
- [Migration from v5.x](#migration-from-v5x)
- [Plugin Dependencies](#plugin-dependencies)
- [Contributing](#contributing)
- [License](#license)

## Installation

1. Clone or download this repository into your project's `Plugins/` folder:
   ```
   YourProject/
     Plugins/
       Paper2DPlus/
         Paper2DPlus.uplugin
         Source/
         Config/
         Resources/
   ```
2. Regenerate project files (right-click your `.uproject` > Generate Visual Studio project files).
3. Build the project. Paper2D Plus has two modules:
   - **Paper2DPlus** (Runtime) -- data types, components, Blueprint library
   - **Paper2DPlusEditor** (Editor) -- asset editor, sprite extractor, Aseprite importer

## Quick Start

1. **Extract sprites** from a sprite sheet using the Sprite Extractor (`Window > Paper2DPlus > Sprite Extractor`, or right-click a texture > `Paper2D+ Actions > Extract Sprites`).
2. **Create a Character Profile Asset**: right-click in Content Browser > `Paper2DPlus > Character Profile Asset`.
3. **Add flipbooks**: Open the asset, go to the Overview tab, and add flipbooks. Link each to a Paper2D Flipbook.
4. **Edit hitboxes**: Select a flipbook, click "Edit Hitboxes" to open the Hitbox Editor tab. Draw attack, hurtbox, and collision rectangles on each frame.
5. **Set up alignment**: Use the Alignment Editor tab to adjust per-frame sprite offsets.
6. **Adjust timing**: Use the Frame Timing tab to fine-tune per-frame durations with the visual timeline.
7. **Organize with groups**: Use Flipbook Groups in the Overview tab to visually organize flipbooks by category (e.g., "Attacks", "Movement").
8. **Add to your actor**: Add a `Paper2DPlus Character Profile` component to your character Blueprint and assign the Character Profile Asset.
9. **Check collisions at runtime**: Call `CheckAttackCollision` or `QuickHitCheck` from the Blueprint Function Library -- they auto-resolve everything from the component.

## Sprite Extraction

Drop in a sprite sheet and Paper2D Plus detects individual sprites automatically.

**Detection modes:**
- **Island detection** -- Flood-fill based with configurable alpha threshold, 8-directional connectivity, and island merging for sprites with gaps.
- **Grid splitting** -- Uniform grid for evenly spaced sprite sheets.

**Interactive canvas:**
- Verify and adjust detected bounds
- Merge regions (Shift+click to select multiple, then M to merge)
- Draw new boxes manually (Ctrl+drag)
- Resize existing boxes with drag handles
- Undo/redo support (Ctrl+Z / Ctrl+Shift+Z)

**Output:**
- Extracted sprites saved as Paper2D Sprite assets
- Optional automatic Flipbook assembly
- Optional integration with Character Profile Assets
- Create Subfolder toggle for organized output
- Folder picker for custom output paths

**Naming system:** Configure naming patterns with a split-point picker for organized output.

**Context menu:** Right-click any texture in the Content Browser and select `Paper2D+ Actions > Extract Sprites` to jump directly into extraction. The same submenu also provides `Import Aseprite File`.

## Character Profile Asset Editor

A 4-tab dockable asset editor for managing all flipbook data. Opens as a dockable tab within the UE editor (single-instance -- re-opening the same asset focuses the existing tab).

### Overview Tab

Grid view of all flipbooks with animated thumbnail cards:
- Hover-animated flipbook previews with checkerboard transparency backgrounds
- Add/remove flipbooks via toolbar buttons with a flipbook picker
- Quick-access buttons to jump to Hitbox/Alignment/Timing editing for any flipbook
- Search, rename (double-click to inline rename), reorder, and duplicate flipbooks
- Delete key removes selected flipbook(s) with undo support
- Multi-select with Ctrl+click and Shift+click
- Context menus on flipbook cards and group headers
- [Flipbook Groups](#flipbook-groups) panel for visual organization
- [Flipbook Tag Mappings](#flipbook-tag-mappings) panel for binding GameplayTags

### Hitbox Editor Tab

Zoomable, pannable 2D canvas with a unified edit tool:

**Hitboxes tool (draw + edit combined):**
- Drag on empty space to create new rectangles
- Click existing hitboxes to select, Shift+click for multi-select
- Move, resize with handles, nudge with arrow keys (Shift for 10x)
- Delete with Delete key
- Choose hitbox type: Attack (red), Hurtbox (green), Collision (blue)
- Properties panel shows position, dimensions, damage, knockback, Z/Depth
- 16px grid snapping
- Visibility filtering: toggle Attack/Hurtbox/Collision independently via toolbar checkboxes

**Socket tool:**
- Click to place named attachment points (e.g., "Muzzle", "Hand", "Foot")
- Sockets carry X/Y position relative to sprite origin

**Frame multi-select:**
- Ctrl+click and Shift+click on frame thumbnails in the horizontal frame strip
- Batch operations apply to all selected frames: copy hitboxes to remaining, batch damage/knockback

**Hitbox properties:**
| Property | Type | Description |
|----------|------|-------------|
| Type | EHitboxType | Attack, Hurtbox, or Collision |
| X, Y | int32 | Position relative to sprite origin |
| Width, Height | int32 | Dimensions in pixels |
| Z, Depth | int32 | Depth offset and thickness for 2.5D |
| Damage | int32 | Damage value (Attack type) |
| Knockback | int32 | Knockback force (Attack type) |

**Hitbox bounds clamping:** Hitboxes are automatically clamped to sprite dimensions during batch copy operations, preventing hitboxes from extending beyond the source sprite area.

**3D Viewport:** Visualizes depth (Z) offsets when 3D Depth is enabled in Project Settings.

### Alignment Editor Tab

Per-frame sprite offsets for precise alignment:
- Drag on canvas or use spinbox controls for precise values
- Onion skinning overlays adjacent frames for alignment reference
- Cross-animation onion skin shows the previous flipbook's trailing frames in purple tint for seamless transitions
- Copy/paste offsets between frames
- Batch apply offsets to selected frames
- Unapplied offset indicators show when changes have not been saved
- Frame drag-and-drop reorder within a flipbook

**Playback modes:**
- Standard forward playback
- Ping-pong (forward + reverse) playback
- Forward onion skin for previewing upcoming frames

**Multi-flipbook playback queue:**
- Drag flipbooks from the sidebar into a playback queue to preview transitions
- Reorder queue entries via drag-and-drop or right-click context menu (Move Up / Move Down)
- Time-based playback respects per-frame durations across queued flipbooks
- Right-click flipbooks in the sidebar to quickly add them to the queue

**Navigation:**
- Universal arrow keys across all editor tabs
- Arrow keys wrap across flipbook boundaries -- pressing right on the last frame advances to the next flipbook's first frame (and vice versa)
- Navigation follows queue order when a queue is active, list order otherwise
- Search bar filters the flipbook list for quick access in large sets

### Frame Timing Tab

Visual timeline for per-frame duration control:
- Proportional color-coded duration blocks with FPS-aware coloring (Standard, Slight Hold, Medium Hold, Long Hold)
- Drag handles to adjust frame durations
- Toggle between frame count and millisecond display
- Playback preview with adjustable FPS and offset-aware sprite rendering
- Frame multi-select with Ctrl+click and Shift+click
- Batch tools in a dedicated side panel: set all frames, distribute evenly, reset to default, apply to selected

## Flipbook Groups

Visual organization system for flipbooks within the Overview tab:

- **Collapsible groups** with customizable names and colors
- **Nested groups** -- groups can have parent groups for hierarchical organization
- **Drag-and-drop** -- move flipbooks between groups, reorder within groups
- **Multi-select** -- Ctrl+click and Shift+click for selecting multiple flipbooks
- **Auto-group by prefix** -- automatically create groups based on flipbook name prefixes (e.g., "Attack_Slash" and "Attack_Thrust" auto-group into "Attack")
- **Inline rename** -- double-click group headers to rename
- **Search filtering** -- filter flipbooks across all groups
- **Persistent** -- group assignments are saved on the Character Profile Asset

Groups are purely visual organization -- they do not affect runtime behavior or tag mappings.

## Flipbook Tag Mappings

Bind GameplayTags to flipbooks for structured lookups:

**Panel features:**
- Required tags auto-populated from Project Settings -- no manual setup needed
- Alphabetical card layout with tag name, badges, and assigned flipbooks
- Drag-and-drop flipbook assignment onto tag cards from the Overview tab
- Per-flipbook PaperZD animation sequence pickers alongside flipbook-to-tag bindings

**Use cases:**
- **Combo systems** -- Array order in each tag is significant (index 0 = first hit, index 1 = second, etc.)
- **AI decisions** -- Query max attack range per tag for engagement distance
- **PaperZD integration** -- Optional PaperZD AnimSequence per flipbook within each tag
- **Arbitrary metadata** -- Key-value `TMap<FName, TSoftObjectPtr<UObject>>` per tag for sound cues, montages, etc.

**Project Settings integration:** Define required tags in `Project Settings > Plugins > Paper2DPlus`. The editor auto-populates required tag cards and warns when a Character Profile Asset is missing required tag mappings.

## Frame Exclusion

Non-destructive frame management:

- **Exclude** individual frames from a flipbook without deleting them -- excluded frames are hidden from playback and hitbox editing but preserved in the asset
- **Restore** excluded frames at any time with their original data intact
- Reference frame identity is preserved after exclusion -- frame indices don't shift
- Exclude/restore actions available via context menu and alignment editor overlays

## Asset Validation

Character Profile Assets are validated on save via UE's DataValidation subsystem:

- Duplicate flipbook names
- Empty hitbox frames
- Missing flipbook references
- Orphaned extraction data
- Missing required tag mappings

Validation issues are surfaced with severity levels (Error, Warning, Info) in the standard UE validation UI.

## Runtime Components

### Character Profile Component

`UPaper2DPlusCharacterProfileComponent` -- Add to any actor with a `PaperFlipbookComponent`.

```
Add Component > Paper2DPlus Character Profile
```

**Properties:**
| Property | Type | Description |
|----------|------|-------------|
| CharacterProfile | UPaper2DPlusCharacterProfileAsset* | The character profile asset |
| FlipbookComponent | UPaperFlipbookComponent* | Auto-found at BeginPlay if not set |

This component enables all actor-based Blueprint functions (`CheckAttackCollision`, `QuickHitCheck`, `GetHitboxFrame`, `GetActorHitboxes`, etc.) to auto-resolve context without passing explicit parameters.

### Debug Component

`UPaper2DPlusDebugComponent` -- Drop-in runtime visualization.

```
Add Component > Paper2DPlus Debug Draw
```

**Properties:**
| Property | Default | Description |
|----------|---------|-------------|
| bEnableDebugDraw | true | Master toggle |
| bDrawAttackHitboxes | true | Show attack boxes |
| bDrawHurtboxes | true | Show hurtboxes |
| bDrawCollisionBoxes | true | Show collision boxes |
| bDrawSockets | true | Show socket positions |
| AttackColor | Red | Attack hitbox color |
| HurtboxColor | Green | Hurtbox color |
| CollisionColor | Blue | Collision box color |
| SocketColor | Yellow | Socket marker color |
| LineThickness | 2.0 | Debug line thickness |

When the owning actor also has a `UPaper2DPlusCharacterProfileComponent`, the debug component automatically derives CharacterProfile, flip state, and scale from the actor each frame.

**Non-uniform scale:** Debug visualization supports separate X and Y scale values, matching actual collision results when `ScaleX != ScaleY`.

Stripped from Shipping builds automatically (`UE_BUILD_SHIPPING` guard).

## Blueprint API Reference

All functions are in `UPaper2DPlusBlueprintLibrary` under the `Paper2DPlus` category.

### Collision Detection

**Actor-based (recommended)** -- Auto-resolves context from `UPaper2DPlusCharacterProfileComponent`:

| Function | Returns | Description |
|----------|---------|-------------|
| `CheckAttackCollision(Attacker, Defender, OutResults)` | bool | Full collision check with detailed results per overlap |
| `QuickHitCheck(Attacker, Defender)` | bool | Fast boolean check, no detailed results |
| `GetHitboxFrame(Actor, OutFrameData)` | bool | Get current frame hitbox data for an actor |

**Frame-data based** -- For manual/advanced usage:

| Function | Returns | Description |
|----------|---------|-------------|
| `CheckHitboxCollision(AttackerFrame, ..., DefenderFrame, ..., OutResults)` | bool | Full collision from frame data |
| `CheckHitboxCollision3D(...)` | bool | Same but with FVector positions (uses X and Z) |
| `QuickHitCheckFromFrames(...)` | bool | Fast boolean check from frame data |
| `DoBoxesOverlap(BoxA, BoxB)` | bool | Raw FBox2D overlap test |

**Collision result struct** (`FHitboxCollisionResult`):
| Field | Type | Description |
|-------|------|-------------|
| bHit | bool | Did collision occur |
| AttackHitbox | FHitboxData | The attack box that connected |
| HurtHitbox | FHitboxData | The hurtbox that was hit |
| HitLocation | FVector2D | World-space center of overlap |
| Damage | int32 | Damage from the attack hitbox |
| Knockback | int32 | Knockback from the attack hitbox |

### World-Space Hitbox API

Actor-based functions that return hitboxes and sockets already converted to world space. Auto-resolves position, flip, and scale from the actor's `UPaper2DPlusCharacterProfileComponent` and `UPaperFlipbookComponent`.

| Function | Returns | Description |
|----------|---------|-------------|
| `GetActorHitboxes(Actor, OutHitboxes)` | bool | All hitboxes on current frame as `TArray<FWorldHitbox>` |
| `GetActorAttackBoxes(Actor, OutHitboxes)` | bool | Attack hitboxes only |
| `GetActorHurtboxes(Actor, OutHitboxes)` | bool | Hurtboxes only |
| `GetActorCollisionBoxes(Actor, OutHitboxes)` | bool | Collision boxes only |
| `GetActorSockets(Actor, OutSockets)` | bool | All sockets as `TArray<FWorldSocket>` |
| `GetActorSocketByName(Actor, SocketName, OutLocation)` | bool | Single socket world position by name |

### Frame Data Helpers

**Actor-based:**

| Function | Returns | Description |
|----------|---------|-------------|
| `GetFrameDamage(Actor)` | int32 | Total damage of all attack hitboxes on current frame |
| `GetFrameKnockback(Actor)` | int32 | Max knockback on current frame |
| `GetFrameDamageAndKnockback(Actor, OutDamage, OutKnockback)` | bool | Both values in one call |
| `FrameHasAttack(Actor)` | bool | Does current frame have any attack hitboxes |
| `IsFrameInvulnerable(Actor)` | bool | Is current frame marked invulnerable (i-frames) |

**From FFrameHitboxData:**

| Function | Returns | Description |
|----------|---------|-------------|
| `GetAttackHitboxes(FrameData)` | TArray\<FHitboxData\> | All attack-type hitboxes |
| `GetHurtboxes(FrameData)` | TArray\<FHitboxData\> | All hurtbox-type hitboxes |
| `GetCollisionBoxes(FrameData)` | TArray\<FHitboxData\> | All collision-type hitboxes |
| `HasAttackHitboxes(FrameData)` | bool | Has any attack hitboxes |
| `HasHurtboxes(FrameData)` | bool | Has any hurtboxes |
| `HasAnyData(FrameData)` | bool | Has any hitboxes or sockets |

### World Space Conversion

| Function | Returns | Description |
|----------|---------|-------------|
| `HitboxToWorldSpace(Hitbox, WorldPos2D, bFlipX, ScaleX, ScaleY)` | FBox2D | Hitbox to world-space box |
| `HitboxToWorldSpace3D(Hitbox, WorldPos3D, bFlipX, ScaleX, ScaleY)` | FBox2D | Same with FVector (uses X, Z) |
| `SocketToWorldSpace(Socket, WorldPos2D, bFlipX, ScaleX, ScaleY)` | FVector2D | Socket to world position |
| `SocketToWorldSpace3D(Socket, WorldPos3D, bFlipX, ScaleX, ScaleY)` | FVector | Socket to world position (3D) |

All conversion functions handle horizontal flipping automatically when `bFlipX` is true. Scale accepts separate X and Y values for non-uniform scaling.

### Tag Mapping Lookups

Accessed via the Character Profile Asset (not the Blueprint library):

| Function | Returns | Description |
|----------|---------|-------------|
| `GetFlipbookDataForTag(Tag)` | TArray\<FFlipbookHitboxData\> | All flipbook data for a tag (array order = combo order) |
| `GetFlipbooksForTag(Tag)` | TArray\<UPaperFlipbook*\> | All loaded flipbooks for a tag |
| `GetFirstFlipbookForTag(Tag)` | UPaperFlipbook* | First flipbook, or nullptr |
| `GetRandomFlipbookForTag(Tag)` | UPaperFlipbook* | Random flipbook (not network-safe) |
| `GetPaperZDSequenceForTag(Tag)` | UObject* | PaperZD AnimSequence (loads synchronously) |
| `GetTagMappingMetadata(Tag, Key)` | UObject* | Arbitrary metadata by key (loads synchronously) |
| `GetTagMappingMetadataKeys(Tag)` | TArray\<FName\> | All metadata keys for a tag |
| `HasTagMappingMetadata(Tag, Key)` | bool | Check if metadata key exists for a tag |
| `HasTagMapping(Tag)` | bool | Is this tag mapped |
| `GetAllMappedTags()` | TArray\<FGameplayTag\> | All mapped tags |
| `GetFlipbookCountForTag(Tag)` | int32 | Number of flipbooks in a tag |
| `GetTagMapping(Tag, OutBinding)` | bool | Full binding struct |

### Attack Bounds (AI Helpers)

Query the maximum reach of attack hitboxes for AI engagement distance decisions:

| Function | Returns | Description |
|----------|---------|-------------|
| `GetMaxAttackRange()` | float | Max attack range across all flipbooks |
| `GetAttackRangeForTag(Tag)` | float | Max attack range for a specific tag |
| `GetAttackRangeForFlipbook(Name)` | float | Max attack range for a specific flipbook |
| `GetAttackBoundsForTag(Tag)` | FBox2D | Combined bounds of all attack hitboxes in a tag |
| `GetAttackBoundsForFlipbook(Name)` | FBox2D | Combined bounds for a flipbook |

### Debug Visualization

| Function | Description |
|----------|-------------|
| `DrawActorDebugHitboxes(WorldContext, Actor, Duration, Thickness, bDrawSockets)` | Draw hitboxes for an actor's current frame |
| `DrawDebugHitboxes(WorldContext, FrameData, WorldPos, bFlipX, ScaleX, ScaleY, ...)` | Draw hitboxes from frame data |
| `DrawDebugHitbox(WorldContext, Hitbox, WorldPos, bFlipX, ScaleX, ScaleY, Color, ...)` | Draw a single hitbox |

All debug functions are `DevelopmentOnly` -- stripped from Shipping builds. Scale accepts separate X and Y values for non-uniform rendering.

### Serialization

Character Profile Assets support JSON import/export:

| Function | Description |
|----------|-------------|
| `ExportToJsonString(OutJson)` | Export to JSON string |
| `ImportFromJsonString(JsonString)` | Import from JSON string |
| `ExportToJsonFile(FilePath)` | Export to a JSON file |
| `ImportFromJsonFile(FilePath)` | Import from a JSON file |

JSON schema is versioned (current: v5) with automatic migration from older versions.

### Validation

| Function | Returns | Description |
|----------|---------|-------------|
| `ValidateCharacterProfileAsset(OutIssues)` | bool | Validate asset for common issues (missing flipbooks, unmapped tags, etc.) |
| `TrimTrailingFrameData(FlipbookIndex)` | int32 | Remove excess frame data beyond flipbook keyframe count |
| `TrimAllTrailingFrameData()` | int32 | Trim all flipbooks |

### Frame Exclusion

| Function | Description |
|----------|-------------|
| `ExcludeFlipbookFrame(FlipbookIndex, FrameIndex)` | Exclude a frame (non-destructive) |
| `RestoreExcludedFlipbookFrame(FlipbookIndex, SourceFrameIndex)` | Restore a previously excluded frame |

### Batch Operations

| Function | Description |
|----------|-------------|
| `CopyFrameDataToRange(FlipbookName, SourceFrame, Start, End, bIncludeSockets)` | Copy hitboxes/sockets from one frame to a range |
| `MirrorHitboxesInRange(FlipbookName, Start, End, PivotX)` | Mirror hitboxes horizontally |
| `ClampFrameHitboxesToSpriteBounds(FlipbookName, FrameIndex)` | Clamp hitboxes to sprite dimensions |

## Project Settings

`Project Settings > Plugins > Paper2DPlus`

| Setting | Type | Description |
|---------|------|-------------|
| RequiredTagMappings | TArray\<FGameplayTag\> | Tags every character should map (editor auto-populates and shows warnings for unmapped) |
| GroupDescriptions | TArray\<FGroupDescriptionMapping\> | Optional tooltips per tag shown in the editor |
| bEnable3DDepth | bool | Enable Z/Depth fields on hitboxes and the 3D viewport |

## Aseprite Import

Paper2D Plus includes an Aseprite (.ase/.aseprite) file importer accessible from:
- `Window > Paper2DPlus > Import Aseprite File`
- Right-click a texture > `Paper2D+ Actions > Import Aseprite File`

The importer reads:
- Frames and cel data
- Layer information
- Palette and color data
- Tag/animation ranges

Imported data creates sprites and flipbooks matching the Aseprite file structure.

## Data Types Reference

### Enums

| Enum | Values | Header |
|------|--------|--------|
| `EHitboxType` | Attack, Hurtbox, Collision | Paper2DPlusTypes.h |
| `ESpriteAnchor` | TopLeft, TopCenter, TopRight, CenterLeft, Center, CenterRight, BottomLeft, BottomCenter, BottomRight, None | Paper2DPlusTypes.h |
| `ECharacterProfileValidationSeverity` | Info, Warning, Error | Paper2DPlusCharacterProfileAsset.h |

### Structs

| Struct | Description | Header |
|--------|-------------|--------|
| `FHitboxData` | Single hitbox: type, position, dimensions, Z/depth, damage, knockback | Paper2DPlusTypes.h |
| `FSocketData` | Named attachment point: name, X, Y | Paper2DPlusTypes.h |
| `FFrameHitboxData` | All hitboxes and sockets for one frame, plus invulnerability flag | Paper2DPlusTypes.h |
| `FWorldHitbox` | World-space hitbox with pre-computed bounds (from actor-based API) | Paper2DPlusTypes.h |
| `FWorldSocket` | World-space socket with pre-computed position (from actor-based API) | Paper2DPlusTypes.h |
| `FHitboxCollisionResult` | Collision check output: hit flag, attack/hurt boxes, location, damage, knockback | Paper2DPlusTypes.h |
| `FSpriteExtractionInfo` | Per-frame extraction metadata: source offset, threshold, padding, alignment offsets, flip, SourceFrameIndex, bExcludedFromFlipbook | Paper2DPlusTypes.h |
| `FFlipbookGroupInfo` | Visual group: name, parent, color | Paper2DPlusCharacterProfileAsset.h |
| `FFlipbookHitboxData` | Full flipbook entry: name, flipbook ref, frames array, source texture, extraction info, group assignment, ExcludedFrames | Paper2DPlusCharacterProfileAsset.h |
| `FFlipbookTagMapping` | Tag binding: flipbook names, PaperZD sequences (parallel array), metadata map | Paper2DPlusCharacterProfileAsset.h |
| `FExcludedFlipbookFrameData` | Preserved data for an excluded frame: source index, keyframe, hitbox data | Paper2DPlusCharacterProfileAsset.h |
| `FCharacterProfileValidationIssue` | Validation result: severity, context, message | Paper2DPlusCharacterProfileAsset.h |

### Classes

| Class | Type | Description |
|-------|------|-------------|
| `UPaper2DPlusCharacterProfileAsset` | UPrimaryDataAsset | Central data asset holding all flipbooks, hitboxes, groups, tag mappings |
| `UPaper2DPlusCharacterProfileComponent` | UActorComponent | Add to actors to provide hitbox context |
| `UPaper2DPlusDebugComponent` | UActorComponent | Runtime debug visualization |
| `UPaper2DPlusBlueprintLibrary` | UBlueprintFunctionLibrary | All Blueprint-callable functions |
| `UPaper2DPlusSettings` | UDeveloperSettings | Project-wide settings |
| `UPaper2DPlusCharacterProfileAssetValidator` | UEditorValidatorBase | DataValidation integration for Character Profile Assets |

## Migration from v5.x

v6.0 renames `CharacterData` to `CharacterProfile` across the entire public API:

| v5.x | v6.0 |
|------|------|
| `UPaper2DPlusCharacterDataAsset` | `UPaper2DPlusCharacterProfileAsset` |
| `UPaper2DPlusCharacterDataComponent` | `UPaper2DPlusCharacterProfileComponent` |
| `SetActorCharacterData()` | `SetActorCharacterProfile()` |
| All `CharacterData` header files | Renamed to `CharacterProfile` |

**Existing assets load automatically** -- CoreRedirects in `Config/DefaultPaper2DPlus.ini` handle seamless migration with no manual steps. C++ consumers must update `#include` paths and symbol references.

## Plugin Dependencies

- **Paper2D** (engine built-in) -- Core 2D sprite and flipbook system
- **GameplayTagsEditor** (engine built-in) -- Provides the tag picker widget used in Flipbook Tag Mappings
- **DataValidation** (engine built-in) -- Asset validation subsystem integration

All ship with Unreal Engine and require no additional installation.

## Support

- [Discord](https://discord.com/invite/eJAyFthTNs)
- [YouTube](https://www.youtube.com/@infinitegameworks)

## Contributing

Contributions are welcome. Please open an issue to discuss changes before submitting a pull request.

## License

Copyright 2026 Infinite Gameworks. All Rights Reserved.

See [LICENSE](LICENSE) for details.
