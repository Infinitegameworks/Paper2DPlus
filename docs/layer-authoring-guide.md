# Character Layer Authoring

The Character Layer Asset is the source of layered character art and Layer-owned gameplay data. The model is deliberately small: globally ordered **Layers**, optional **Exclusive Groups**, complete **Appearance Presets**, and one required **Default Appearance**.

![Layer Workspace with Structure, Preview, and focused authoring inspector](images/designer-workflow-overhaul/10-layer-authoring-workspace.png)

## The model

- A **Layer** is one stable selectable unit. Its `LayerId` survives renames and is the identity used by presets, runtime state, save data, and replication.
- The `Layers` array is the only paint and gameplay precedence order. Move a Layer in Structure to change that order. Every row shows its position in that global order, and **Sections never change it** — so moving a Layer past a member of a different Section changes its number without moving its row.
- An **Exclusive Group** is optional. Activating one member deactivates any active peer; the group may also be empty.
- An **Appearance Preset** is a complete snapshot. Applying it disables every Layer omitted from the preset.
- The **Default Appearance** is required and supplies the initial selection for both delivery modes.
- A selected Layer contributes only on animations for which it owns authored art or gameplay rows.

![Appearance mode with complete presets, Default Appearance, and optional Exclusive Groups](images/designer-workflow-overhaul/11-layer-appearance-workspace.png)

## Choose one delivery mode

| Mode | Use it for | Runtime behavior |
|---|---|---|
| Fixed / Baked | One published appearance | Bake compiles the Default Appearance into the existing canonical Profile and Flipbooks. Runtime selection mutation is rejected. |
| Runtime Customizable | Appearance changes during play | The component initializes from the Default Appearance and accepts preset or individual Layer changes. |

The modes are mutually exclusive. A character never renders or composes both paths.

## Finding a Layer

The **Structure** dock lists every Layer in one stack, headed by the total count, a search box, and
**+ Section**.

**Sections** group Layers for authoring and nothing else. A Section has a name and a member count, it
collapses, and its checkbox previews every Layer inside it at once. Renaming, collapsing, or moving a
Layer between Sections changes no paint order, no runtime state, no appearance selection, and no bake
output — which is why every row keeps showing its global position. Layers with no Section gather under
a derived **Ungrouped** heading that reads "N to sort"; it always sits last and cannot be renamed or
deleted. Deleting a Section keeps its Layers and returns them to Ungrouped.

**Search** matches Layer names — including the source path an import gives them, so `jacket` finds
`Clothes/Jacket/Front` — and Section names. A Section is never hidden by a filter: it stays visible as
its matches' heading, and matching a Section's own name keeps everything inside it.

**Drag a Layer row** onto a Section header to move it into that Section, or into the gap between two
rows to place it precisely in the global order. An insertion marker shows where it will land, and a
drag that crosses a Section boundary is a single undo step. Dropping onto **Ungrouped** takes a Layer
back out of its Section; a drop that would change nothing is refused rather than quietly no-op'ing.

**Clicking the character in the Art view selects the Layer you clicked.** Hovering tints that Layer's
art so you can see what you are about to pick, and selecting it expands its Section and scrolls its
row into view. Picking is by the art's own pixels, so clicking a transparent spot selects nothing
rather than guessing at whatever is on top.

At narrow widths the row sheds its position number, then its Section count, then its actions menu —
the Layer name and its preview checkbox are the last things to go.

**Section suggestions on import.** In the Bulk Sprite Extractor, an Aseprite row's **Edit…** window
offers **Preview Section Suggestions…**: one proposed Section per top-level folder in that file, since
a Layer's name *is* its Aseprite group path. It proposes one level only — `Armor/Pauldron/Left` joins
**Armor**, not an invented `Armor/Pauldron` — and a Layer at the file's root is left ungrouped rather
than swept into a catch-all.

Nothing is written until you press **Apply**. Before that, tick or untick each proposal, rename it
freely (renaming is only the Section's label; the folder stays the key, so reopening the preview keeps
your name), and remove any you do not want. A proposal whose name a Section already has says "merges"
and joins that Section instead of creating a second one you could not tell apart.

Applying to a Layer Profile that already holds the Layers places them immediately. On a first import
the Layers do not exist yet, so the accepted proposals are applied the moment that file finishes
importing, and the import summary reports how many Layers were placed. Re-applying is harmless: a
Section resolves by name and a Layer is touched only if it actually moves.

**Curated Sections survive reimport.** Reimport never re-derives Sections from folders, so a Layer you
moved by hand stays where you put it, a renamed Layer carries its Section with it, and a new Layer
arrives ungrouped rather than guessing. Because a Section counts as authored work, a Layer that
disappears from the source file is kept and reported instead of being removed.

## Workspace flow

The **Structure** tab stays visible in its own left-side dock while **Art**, **Hitboxes**, **Frame Cues**, and **Appearance** switch in the central tool stack. The narrower right rail keeps contextual Tool Panels above Completion and Related Profiles without taking over the canvas.

The standard top toolbar can start and control PIE directly from the Layer workspace with Play, Pause/Resume, Step, and Stop; these controls run the project session and do not change the transient Layer preview.

1. Assign a Base Profile.
2. Import layered art or add Layers.
3. Use **Structure** to set global order, group Layers into Sections, and set optional Exclusive Groups.
4. Select an animation and Layer.
5. Author **Art**, **Hitboxes**, and **Frame Cues** on that Layer.
6. Open **Appearance** to define complete presets and designate the Default Appearance.
7. Validate, then either publish Fixed / Baked output or use Runtime Customizable delivery.

The preview eye is temporary editor presentation. It is never saved as selection truth, included in a descriptor, replicated, or used by bake. Fixed publishing always consumes the Default Appearance.

## Import and reimport

Import creates generic Layers in source order. It does not infer body regions, equipment kinds, effect kinds, groups, or preset membership from filenames or Aseprite metadata.

Reimport matches existing stable Layers, preserves their current global order and explicit group/preset assignments, updates matched art, and appends genuinely new Layers. Review new Layers and add them to presets deliberately.

Sections are the one thing an import will offer to fill in for you, and only when you ask: see **Section suggestions on import** above.

### Live reimport is incremental

Saving a tracked `.aseprite` file while the editor is open reimports it automatically, and the
reimport regenerates only what actually changed. A save with no visual or timing change writes
nothing. Editing one layer's pixels rebuilds that layer's sheet, the composited sheet, and only
the sprites whose silhouette bounds moved — an inside-the-lines recolor touches no sprites at
all. Retiming or re-ranging a tag rebuilds just that animation's flipbook. A canvas or
frame-count change rebuilds everything, as it must.

Every automatic reimport reports its decisions on the **Aseprite Import** page of the Message
Log — what was rewritten, what was skipped, and why — plus one cost summary line in the Output
Log.

**Live .ase Auto-Reimport** (Project Settings → Plugins → Paper2DPlus → Aseprite Import, on by
default) turns the automatic reaction off entirely. Changed files are still detected by content
hash, and everything that drifted is reimported the moment the setting comes back on, so no edit
is lost while it is off. If generated assets ever look out of step with their source file,
right-click the Layer Profile in the Content Browser and choose **Force Full Reimport** — it
replays the whole import with the change detection bypassed, and it works even while the setting
is off. A Layer Profile built from several `.ase` files has ONE row for any layer name the files share (its sheet holds every file's frames); replaying a single source cannot rebuild that row correctly, so Force Full Reimport refuses a source whose layers are shared with another source and names them - re-import every source together through the Bulk Sprite Extractor (**Paper2D+ Actions > Import Aseprite Files...**) instead. Every import and replay now ends by checking that each recorded sprite reference resolves to an existing asset; a layer whose art was never written is reported as a validation Error naming the layer and the missing frame count, in the import notification, the Output Log and every Validate surface.

## Layer-owned gameplay

Each Layer can author per-animation Hitboxes, Sockets, and behavior-carrying Frame Cues. Runtime Customizable composition walks the selected Layers in global order. Fixed / Baked delivery compiles the same Default Appearance selection into the Profile, so both paths share merge order and animation eligibility.

The composed placement executes its Cue Type behavior immediately before project listeners or receivers are notified. Cue placements remain shared and stateless: read their payload and dispatch context, act on the receiving world, and never store per-playback state on the Cue itself.

## Fixed / Baked publishing

The bottom-right **Bake** menu owns publishing actions. Bake is explicit and updates the registered canonical output in place, preserving object paths and references. It does not save packages automatically; use **Save Bake Set** after reviewing the result.

Exact publishing is fail-closed. Stock Paper2D materials, exact source pixels, supported topology, nearest filtering, and no mipmaps are required. A future manifest or digest version is read-only; an older version requires one complete Bake All upgrade.

Repair, Rebase Registration, Overwrite from Layer Source, Advanced Detach, and Save Bake Set retain their existing ownership and recovery rules. Detach freezes the current output; it does not reconstruct source Layers.

## Validation checklist

- Every Layer has a valid unique `LayerId`.
- Every Exclusive Group reference is valid.
- No preset selects multiple members of one Exclusive Group.
- Every preset contains only existing Layer IDs.
- The Default Appearance exists, is valid, and is complete.
- Authored animation rows still resolve against the Base Profile.
- Delivery configuration and bake ownership are internally consistent.

Validation is read-only. Fixes and publishing are separate explicit transactions.

## Runtime handoff

For Runtime Customizable characters, continue with [Runtime Appearance](runtime-appearance-guide.md). For overall workspace orientation, see the [Designer Guide](designer-guide.md).
