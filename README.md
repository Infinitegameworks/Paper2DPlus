# Paper2D Plus

A character sprite data pipeline and visual editor for **Unreal Engine 5** Paper2D projects.

Paper2D Plus manages a 2D character's entire sprite data lifecycle -- from raw sprite sheet to combat-ready hitboxes -- through a single **Character Data Asset**. Each asset bundles animations, per-frame hitboxes/hurtboxes, sockets, sprite alignment, frame timing, and dimension management in one place.

## Table of Contents

- [Installation](#installation)
- [Quick Start](#quick-start)
- [Sprite Extraction](#sprite-extraction)
- [Character Data Asset Editor](#character-data-asset-editor)
  - [Overview Tab](#overview-tab)
  - [Hitbox Editor Tab](#hitbox-editor-tab)
  - [Alignment Editor Tab](#alignment-editor-tab)
  - [Frame Timing Tab](#frame-timing-tab)
- [Uniform Dimension Management](#uniform-dimension-management)
- [Animation Group Mappings](#animation-group-mappings)
- [Runtime Components](#runtime-components)
  - [Character Data Component](#character-data-component)
  - [Debug Component](#debug-component)
- [Blueprint API Reference](#blueprint-api-reference)
  - [Collision Detection](#collision-detection)
  - [Frame Data Helpers](#frame-data-helpers)
  - [World Space Conversion](#world-space-conversion)
  - [Animation Group Lookups](#animation-group-lookups)
  - [Attack Bounds (AI Helpers)](#attack-bounds-ai-helpers)
  - [Debug Visualization](#debug-visualization)
  - [Serialization](#serialization)
- [Project Settings](#project-settings)
- [Aseprite Import](#aseprite-import)
- [Data Types Reference](#data-types-reference)
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
         Resources/
   ```
2. Regenerate project files (right-click your `.uproject` > Generate Visual Studio project files).
3. Build the project. Paper2D Plus has two modules:
   - **Paper2DPlus** (Runtime) -- data types, components, Blueprint library
   - **Paper2DPlusEditor** (Editor) -- asset editor, sprite extractor, Aseprite importer

## Quick Start

1. **Extract sprites** from a sprite sheet using the Sprite Extractor (`Window > Paper2DPlus > Sprite Extractor`, or right-click a texture in Content Browser).
2. **Create a Character Data Asset**: right-click in Content Browser > `Paper2DPlus > Character Data Asset`.
3. **Add animations**: Open the asset, go to the Overview tab, and add animations. Link each to a Paper2D Flipbook.
4. **Edit hitboxes**: Select an animation, click "Edit Hitboxes" to open the Hitbox Editor tab. Draw attack, hurtbox, and collision rectangles on each frame.
5. **Set up alignment**: Use the Alignment Editor tab to adjust per-frame sprite offsets within uniform dimensions.
6. **Adjust timing**: Use the Frame Timing tab to fine-tune per-frame durations with the visual timeline.
7. **Add to your actor**: Add a `Paper2DPlus Character Data` component to your character Blueprint and assign the Character Data Asset.
8. **Check collisions at runtime**: Call `CheckAttackCollision` or `QuickHitCheck` from the Blueprint Function Library -- they auto-resolve everything from the component.

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

**Output:**
- Extracted sprites saved as Paper2D Sprite assets
- Optional automatic Flipbook assembly
- Uniform sizing pads all frames to consistent dimensions around a chosen anchor point (9 positions: TopLeft through BottomRight)

**Naming system:** Configure naming patterns with a split-point picker for organized output.

## Character Data Asset Editor

A 4-tab custom asset editor for managing all animation data.

### Overview Tab

Grid view of all animations with:
- Sprite thumbnails for each animation
- Flipbook references (soft object pointers -- no hard dependency bloat)
- Quick-access buttons to jump to Hitbox/Alignment/Timing editing for any animation
- Search, rename, reorder, and duplicate animations
- Animation Group Mappings panel for binding GameplayTags

### Hitbox Editor Tab

Zoomable, pannable 2D canvas with three tool modes:

**Draw mode:**
- Click-drag to create rectangles
- 16px grid snapping
- Choose hitbox type before drawing: Attack (red), Hurtbox (green), Collision (blue)

**Edit mode:**
- Click to select, Shift+click for multi-select
- Move, resize with handles, nudge with arrow keys
- Delete with Delete key
- Properties panel shows position, dimensions, damage, knockback, Z/Depth

**Socket mode:**
- Click to place named attachment points (e.g., "Muzzle", "Hand", "Foot")
- Sockets carry X/Y position relative to sprite origin

**Hitbox properties:**
| Property | Type | Description |
|----------|------|-------------|
| Type | EHitboxType | Attack, Hurtbox, or Collision |
| X, Y | int32 | Position relative to sprite origin |
| Width, Height | int32 | Dimensions in pixels |
| Z, Depth | int32 | Depth offset and thickness for 2.5D |
| Damage | int32 | Damage value (Attack type) |
| Knockback | int32 | Knockback force (Attack type) |

**3D Viewport:** Visualizes depth (Z) offsets when 3D Depth is enabled in Project Settings.

**Visibility filtering:** Toggle Attack/Hurtbox/Collision visibility independently via toolbar checkboxes.

**Batch operations:**
- Copy hitboxes to a range of frames
- Mirror hitboxes horizontally
- Propagate across animations
- Bulk set damage/knockback values

### Alignment Editor Tab

Per-frame sprite offsets within uniform dimensions:
- Drag on canvas or use spinbox controls for precise values
- Onion skinning overlays adjacent frames for alignment reference
- Flip per-frame, per-animation, or globally (X and Y)
- Copy/paste offsets between frames
- Batch apply offsets across frame ranges

### Frame Timing Tab

Visual timeline for per-frame duration control:
- Proportional color-coded duration blocks
- Drag handles to adjust frame durations
- Toggle between frame count and millisecond display
- Playback preview with adjustable FPS
- Batch operations: set all frames, distribute evenly, reset to default

## Uniform Dimension Management

Tracks dimension status across all animations with color-coded indicators:
- Calculates largest needed dimensions across all frames
- Applies uniform sizing globally with a single action
- Re-extracts sprites using stored extraction metadata -- no need to re-run detection
- Anchor point selection (9 positions) controls how smaller sprites are padded

## Animation Group Mappings

Bind GameplayTags to animations for structured lookups:

**Use cases:**
- **Combo systems** -- Array order in each group is significant (index 0 = first hit, index 1 = second, etc.)
- **AI decisions** -- Query max attack range per group for engagement distance
- **PaperZD integration** -- Optional soft reference to PaperZD AnimSequence per group
- **Arbitrary metadata** -- Key-value `TMap<FName, TSoftObjectPtr<UObject>>` per group for sound cues, montages, etc.

**Project Settings integration:** Define required animation groups in `Project Settings > Plugins > Paper2DPlus`. The editor warns when a Character Data Asset is missing required group mappings.

## Runtime Components

### Character Data Component

`UPaper2DPlusCharacterDataComponent` -- Add to any actor with a `PaperFlipbookComponent`.

```
Add Component > Paper2DPlus Character Data
```

**Properties:**
| Property | Type | Description |
|----------|------|-------------|
| CharacterData | UPaper2DPlusCharacterDataAsset* | The character's data asset |
| FlipbookComponent | UPaperFlipbookComponent* | Auto-found at BeginPlay if not set |

This component enables all actor-based Blueprint functions (`CheckAttackCollision`, `QuickHitCheck`, `GetHitboxFrame`, etc.) to auto-resolve context without passing explicit parameters.

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

When the owning actor also has a `UPaper2DPlusCharacterDataComponent`, the debug component automatically derives CharacterData, flip state, and scale from the actor each frame.

Stripped from Shipping builds automatically (`UE_BUILD_SHIPPING` guard).

## Blueprint API Reference

All functions are in `UPaper2DPlusBlueprintLibrary` under the `Paper2DPlus` category.

### Collision Detection

**Actor-based (recommended)** -- Auto-resolves context from `UPaper2DPlusCharacterDataComponent`:

| Function | Returns | Description |
|----------|---------|-------------|
| `CheckAttackCollision(Attacker, Defender, OutResults)` | bool | Full collision check with detailed results per overlap |
| `QuickHitCheck(Attacker, Defender)` | bool | Fast boolean check, no detailed results |
| `GetHitboxFrame(Actor, OutFrameData)` | bool | Get current frame's hitbox data for an actor |

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
| `HitboxToWorldSpace(Hitbox, WorldPos2D, bFlipX, Scale)` | FBox2D | Hitbox to world-space box |
| `HitboxToWorldSpace3D(Hitbox, WorldPos3D, bFlipX, Scale)` | FBox2D | Same with FVector (uses X, Z) |
| `SocketToWorldSpace(Socket, WorldPos2D, bFlipX, Scale)` | FVector2D | Socket to world position |
| `SocketToWorldSpace3D(Socket, WorldPos3D, bFlipX, Scale)` | FVector | Socket to world position (3D) |

All conversion functions handle horizontal flipping automatically when `bFlipX` is true.

### Animation Group Lookups

Accessed via the Character Data Asset (not the Blueprint library):

| Function | Returns | Description |
|----------|---------|-------------|
| `GetAnimationsForGroup(Tag)` | TArray\<FAnimationHitboxData\> | All animation data for a group (array order = combo order) |
| `GetFlipbooksForGroup(Tag)` | TArray\<UPaperFlipbook*\> | All loaded flipbooks for a group |
| `GetFirstFlipbookForGroup(Tag)` | UPaperFlipbook* | First flipbook, or nullptr |
| `GetRandomFlipbookForGroup(Tag)` | UPaperFlipbook* | Random flipbook (not network-safe) |
| `GetPaperZDSequenceForGroup(Tag)` | UObject* | PaperZD AnimSequence (loads synchronously) |
| `GetGroupMetadata(Tag, Key)` | UObject* | Arbitrary metadata by key (loads synchronously) |
| `GetGroupMetadataKeys(Tag)` | TArray\<FName\> | All metadata keys for a group |
| `HasGroup(Tag)` | bool | Is this group mapped |
| `GetAllMappedGroups()` | TArray\<FGameplayTag\> | All mapped group tags |
| `GetAnimationCountForGroup(Tag)` | int32 | Number of animations in a group |
| `GetGroupBinding(Tag, OutBinding)` | bool | Full binding struct |
| `GetUnmappedRequiredGroups(Asset)` | TArray\<FGameplayTag\> | Required groups not yet mapped (from Project Settings) |

### Attack Bounds (AI Helpers)

Query the maximum reach of attack hitboxes for AI engagement distance decisions:

| Function | Returns | Description |
|----------|---------|-------------|
| `GetMaxAttackRange()` | float | Max attack range across all animations |
| `GetAttackRangeForGroup(Tag)` | float | Max attack range for a specific group |
| `GetAttackRangeForAnimation(Name)` | float | Max attack range for a specific animation |
| `GetAttackBoundsForGroup(Tag)` | FBox2D | Combined bounds of all attack hitboxes in a group |
| `GetAttackBoundsForAnimation(Name)` | FBox2D | Combined bounds for an animation |

### Debug Visualization

| Function | Description |
|----------|-------------|
| `DrawActorDebugHitboxes(WorldContext, Actor, Duration, Thickness, bDrawSockets)` | Draw hitboxes for an actor's current frame |
| `DrawDebugHitboxes(WorldContext, FrameData, WorldPos, bFlipX, Scale, ...)` | Draw hitboxes from frame data |
| `DrawDebugHitbox(WorldContext, Hitbox, WorldPos, bFlipX, Scale, Color, ...)` | Draw a single hitbox |

All debug functions are `DevelopmentOnly` -- stripped from Shipping builds.

### Serialization

Character Data Assets support JSON import/export:

| Function | Description |
|----------|-------------|
| `ExportToJsonString(OutJson)` | Export to JSON string |
| `ImportFromJsonString(JsonString)` | Import from JSON string |
| `ExportToJsonFile(FilePath)` | Export to a JSON file |
| `ImportFromJsonFile(FilePath)` | Import from a JSON file |

JSON schema is versioned (current: v3) with automatic migration from older versions.

### Validation

| Function | Returns | Description |
|----------|---------|-------------|
| `ValidateCharacterDataAsset(OutIssues)` | bool | Validate asset for common issues (missing flipbooks, unmapped groups, etc.) |
| `TrimTrailingFrameData(AnimIndex)` | int32 | Remove excess frame data beyond flipbook keyframe count |
| `TrimAllTrailingFrameData()` | int32 | Trim all animations |

### Batch Operations

| Function | Description |
|----------|-------------|
| `CopyFrameDataToRange(AnimName, SourceFrame, Start, End, bIncludeSockets)` | Copy hitboxes/sockets from one frame to a range |
| `MirrorHitboxesInRange(AnimName, Start, End, PivotX)` | Mirror hitboxes horizontally |
| `SetSpriteFlipInRange(AnimName, Start, End, bFlipX, bFlipY)` | Set flip state for a frame range |
| `SetSpriteFlipForAnimation(AnimName, bFlipX, bFlipY)` | Set flip for entire animation |
| `SetSpriteFlipForAllAnimations(bFlipX, bFlipY)` | Set flip globally |

## Project Settings

`Project Settings > Plugins > Paper2DPlus`

| Setting | Type | Description |
|---------|------|-------------|
| RequiredAnimationGroups | TArray\<FGameplayTag\> | Groups every character should map (editor shows warnings for unmapped) |
| GroupDescriptions | TArray\<FGroupDescriptionMapping\> | Optional tooltips per group tag shown in the editor |
| bEnable3DDepth | bool | Enable Z/Depth fields on hitboxes and the 3D viewport |

## Aseprite Import

Paper2D Plus includes an Aseprite (.ase/.aseprite) file importer that reads:
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
| `ESpriteAnchor` | TopLeft, TopCenter, TopRight, CenterLeft, Center, CenterRight, BottomLeft, BottomCenter, BottomRight | Paper2DPlusTypes.h |
| `ECharacterDataValidationSeverity` | Info, Warning, Error | Paper2DPlusCharacterDataAsset.h |

### Structs

| Struct | Description | Header |
|--------|-------------|--------|
| `FHitboxData` | Single hitbox: type, position, dimensions, Z/depth, damage, knockback | Paper2DPlusTypes.h |
| `FSocketData` | Named attachment point: name, X, Y | Paper2DPlusTypes.h |
| `FFrameHitboxData` | All hitboxes and sockets for one frame, plus invulnerability flag | Paper2DPlusTypes.h |
| `FHitboxCollisionResult` | Collision check output: hit flag, attack/hurt boxes, location, damage, knockback | Paper2DPlusTypes.h |
| `FSpriteExtractionInfo` | Per-frame extraction metadata: source offset, threshold, padding, alignment offsets, flip | Paper2DPlusTypes.h |
| `FAnimationHitboxData` | Full animation: name, flipbook ref, frames array, source texture, extraction info | Paper2DPlusCharacterDataAsset.h |
| `FAnimationGroupBinding` | Group binding: animation names, PaperZD sequence, metadata map | Paper2DPlusCharacterDataAsset.h |
| `FCharacterDataValidationIssue` | Validation result: severity, context, message | Paper2DPlusCharacterDataAsset.h |

### Classes

| Class | Type | Description |
|-------|------|-------------|
| `UPaper2DPlusCharacterDataAsset` | UPrimaryDataAsset | Central data asset holding all animations, hitboxes, groups |
| `UPaper2DPlusCharacterDataComponent` | UActorComponent | Add to actors to provide hitbox context |
| `UPaper2DPlusDebugComponent` | UActorComponent | Runtime debug visualization |
| `UPaper2DPlusBlueprintLibrary` | UBlueprintFunctionLibrary | All Blueprint-callable functions |
| `UPaper2DPlusSettings` | UDeveloperSettings | Project-wide settings |

## Plugin Dependencies

- **Paper2D** (engine built-in) -- Core 2D sprite and flipbook system
- **GameplayTagsEditor** (engine built-in) -- Provides the tag picker widget used in Animation Group Mappings

Both ship with Unreal Engine and require no additional installation.

## Contributing

Contributions are welcome. Please open an issue to discuss changes before submitting a pull request.

## License

Copyright 2026 Infinite Gameworks. All Rights Reserved.

See [LICENSE](LICENSE) for details.
