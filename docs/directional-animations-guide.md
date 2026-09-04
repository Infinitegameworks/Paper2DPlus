# Multidirectional Animations

Multidirectional animation keeps one logical Character Profile animation while giving it optional
art for several facing directions. The base flipbook remains the animation identity used by tags,
transitions, combo chains, hitboxes, root motion, curves, and Frame Cues. Directional flipbooks are
visual variants owned by that base entry; they do not become separate Animation Map moves.

Paper2DPlus stores and resolves this data without PaperZD. A project may play the returned Paper
flipbook directly or map it to a separately authored PaperZD sequence.

## Author a directional set

1. Open a Character Profile and use the **Animations** tool to select the logical animation.
2. In **Details > Directional Animation**, set the Profile-wide **Direction Count** and **Angle
   Offset**. New Profiles use eight evenly spaced slots and zero degrees. Count supports 3 through
   16; offset supports -45 through 45 degrees.
3. Choose **Enable Directional Set** for the selected animation. The set starts configured but empty
   and inherits the Profile settings.
4. Use **Local Settings** only when this animation needs a different count and offset. Count and
   offset override together.
5. Assign flipbooks per slot. The fastest route is **Auto-fill from names...** in
   **Details > Directional Animation**: it fills every empty active slot from flipbooks named
   `<Base>_<N/NE/E/...>` (or `<Base>_<slot index>`, with `_`, `-`, or space separators) in the base
   flipbook's folder, previews the whole mapping, and commits it as one undoable transaction.
   Individual slots are edited on the per-slot rows in the same section (each row is a standard
   asset picker that also accepts drag-and-drop), or from any tool header: open the direction
   wheel, point at a slot, and press **Assign -> <direction>**. **Clear** empties the inspected
   slot. The header shows this cluster only for animations that already have a Directional Set;
   otherwise it offers a single **Add Directions...** entry point.
6. Tick **Mirror** on an occupied slot row to present that slot's art horizontally mirrored — the
   standard way to ship an 8-way set from five authored facings (author N, NE, E, SE, S; assign the
   same art to NW, W, SW and mirror those three). The wheel labels such slots "(mirrored)". The
   resolver returns the flag beside the flipbook; your game applies the flip, typically with actor
   scale, so hitboxes and root motion stay base-owned and untouched. Re-picking a slot's art keeps
   its Mirror setting, and the editor preview shows the authored art unflipped.

A positive **Angle Offset** buckets the incoming facing as that much more clockwise, which rotates
the sector layout counter-clockwise on screen. The sign is deliberately compatible with PaperZD's
Directional Angle Offset, so values copied between the two systems mean the same thing.

The wheel is available in Animations, Hitbox, Sprite, Frame Timing, Frame Cues, and Root Motion. Its
header always names the base animation and explains that gameplay edits apply to every direction.
The committed preview is stored as a physical bearing, so changing tools or selecting an animation
with a different direction count re-resolves the same facing instead of reusing an unrelated slot
number.

You can click the header button and then click a wedge, or hold the remappable **Direction Preview
Wheel** shortcut, point at a wedge, and release the shortcut's primary key. Arrow keys traverse
slots; Home and End select the first and last slot; Enter or Space commits a button-opened wheel;
Escape cancels. Cancelling keeps the previous preview and returns focus to the opener.

## Empty, partial, and removed sets

Direction count describes the full evenly spaced topology; the slot array stores only authored
occupancy. A three-direction set may have one, two, or three assigned slots, and an eight-direction
set does not need eight flipbooks.

- **No directional set:** runtime resolution returns the canonical base flipbook.
- **Configured but empty:** runtime resolution also returns the canonical base flipbook. This state
  is useful while setting up an animation.
- **At least one occupied slot:** the animation is multidirectional. Resolution uses the exact
  selected sector. An empty selected slot returns **Direction Unoccupied**; it never chooses the
  nearest occupied slot or falls back to base art.

Clearing the final occupied slot preserves the configured-empty set. **Remove Directional Set** is
available only after every slot is empty. Reducing a count or changing inheritance cannot silently
strand occupied slots; the editor blocks deactivation and shows every affected animation and
bearing. A change that keeps all occupied slots active but moves their centers requires explicit
confirmation.

## Blueprint access

The cooked runtime library exposes these nodes under **Paper2DPlus > Directional Animation**:

- **Has Multi Direction** (pure) takes a Character Profile and either the base or one of its
  directional flipbooks. It is true only for a structurally valid owner with at least one active
  occupied slot.
- **Resolve Directional Flipbook** (pure) takes the Profile, a base or variant flipbook, and a
  non-zero `Vector2D`. It returns a result enum, the resolved flipbook, the selected slot index,
  and the slot's **Mirror Horizontally** flag (apply the flip in your game; typically actor scale).
- **Get Occupied Direction Slots** (callable) returns loaded slot records — index, flipbook, and
  mirror flag — in ascending slot order. It is the broad operation: every occupied result must load successfully or the node fails
  and clears the output array. It is deliberately impure so its synchronous load reads as an
  execution step, and it doubles as the warm/preload call before a directional actor appears.
- **Get Direction Settings** (pure) reports the animation's effective direction count, angle
  offset, and set presence without loading anything.
- **Get Occupied Direction Slot Indices** (pure) reports occupancy as bare indices without loading
  any art.
- **Resolve Direction Slot Index** (pure) converts a facing vector to a slot index for any
  count/offset pair with the resolver's exact angular math, and **Make Direction From Bearing**
  (pure) builds a vector from a clockwise-from-up bearing in degrees. Together with the two reads
  above, these let a project implement its own nearest/base/hold fallback for empty sectors in a
  handful of nodes.

Every failure clears object, index, and array outputs, with one deliberate exception: **Direction
Unoccupied** keeps the slot-index output reporting WHICH sector resolved empty (its art output stays
cleared), so a caller can act on the miss. A zero or non-finite direction is **Invalid Direction**.
Missing input, an unknown flipbook, ambiguous ownership, malformed Profile data, an empty selected
sector, and a failed soft load have distinct result values.

The Character Profile Component also async-preloads the current animation's occupied directional
variants whenever its animation cache warms, so a facing change during play does not stall on a
synchronous load; the resolver's own synchronous load remains a cold-path safety net.

Direction math matches PaperZD 2.2.4: positive Y is zero degrees, positive rotation proceeds
clockwise, the angle offset and half-sector rounding are applied, and the result wraps into the
authored count. Slot count—not occupied-slot count—defines sector width.

## PaperZD boundary

PaperZD plays `UPaperZDAnimSequence` assets, while the directional resolver returns
`UPaperFlipbook`. Paper2DPlus does not create directional PaperZD sequences, rewrite an AnimBP, or
choose playback state. Project Blueprint or C++ should resolve the Profile flipbook and then use its
own flipbook-to-sequence mapping when PaperZD owns playback. This keeps the Character Profile as the
art/data authority and keeps the feature usable when PaperZD is absent.

The existing multiplayer animation advisory and proxy-apply paths remain base-only because their
payload contains no facing direction. Projects that replicate directional presentation must add
that policy outside this first release.

## Shared gameplay data and validation

Directional flipbooks contribute art only. The owning base entry supplies hitboxes, sockets, root
motion, curves, transitions, tags, Frame Cue timing, and Cue payloads. Consequently every assigned
variant must match the base flipbook frame-for-frame:

- playback rate, key-frame count, per-key-frame duration, and sprite presence must match;
- the untrimmed sprite canvas, the pivot's position in that canvas, pixels-per-unit, and source
  rotation must match — this trim-invariant frame space is what keeps base-owned hitboxes,
  sockets, Cues, and root motion spatially correct on every facing.

Trim rectangles and render bounds may legitimately differ: independently trimmed exports of the
same cells and genuinely different facings both produce different silhouettes. Validation surfaces
such differences as an advisory **Warning** — verify the alignment visually rather than
re-exporting; it no longer blocks assignment or fails the Profile.

Validation also rejects count/offset defects, duplicate or inactive slot keys, missing base or
variant assets, and any flipbook identity shared across different logical owners once directional
art participates. Reusing the same flipbook in several slots of one owner is valid.

Per-direction hitboxes, root motion, curves, Frame Cue payload overrides, Character Layer art,
automatic slot fill from mirrored art, and direction-aware Aseprite import are intentionally
outside this release.
