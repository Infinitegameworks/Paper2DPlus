# Character Layer Authoring

The Character Layer Asset is the source of layered character art and Layer-owned gameplay data. The model is deliberately small: globally ordered **Layers**, optional **Exclusive Groups**, complete **Appearance Presets**, and one required **Default Appearance**.

![Layer Workspace with Structure, Preview, and focused authoring inspector](images/designer-workflow-overhaul/10-layer-authoring-workspace.png)

## The model

- A **Layer** is one stable selectable unit. Its `LayerId` survives renames and is the identity used by presets, runtime state, save data, and replication.
- The `Layers` array is the only paint and gameplay precedence order. Move a Layer in Structure to change that order.
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

## Workspace flow

The **Structure** tab stays visible in its own left-side dock while **Art**, **Hitboxes**, **Frame Cues**, and **Appearance** switch in the central tool stack. The narrower right rail keeps contextual Tool Panels above Completion and Related Profiles without taking over the canvas.

The standard top toolbar can start and control PIE directly from the Layer workspace with Play, Pause/Resume, Step, and Stop; these controls run the project session and do not change the transient Layer preview.

1. Assign a Base Profile.
2. Import layered art or add Layers.
3. Use **Structure** to set global order and optional Exclusive Groups.
4. Select an animation and Layer.
5. Author **Art**, **Hitboxes**, and **Frame Cues** on that Layer.
6. Open **Appearance** to define complete presets and designate the Default Appearance.
7. Validate, then either publish Fixed / Baked output or use Runtime Customizable delivery.

The preview eye is temporary editor presentation. It is never saved as selection truth, included in a descriptor, replicated, or used by bake. Fixed publishing always consumes the Default Appearance.

## Import and reimport

Import creates generic Layers in source order. It does not infer body regions, equipment kinds, effect kinds, groups, or preset membership from filenames or Aseprite metadata.

Reimport matches existing stable Layers, preserves their current global order and explicit group/preset assignments, updates matched art, and appends genuinely new Layers. Review new Layers and add them to presets deliberately.

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
