---
module: Paper2DPlus
tags: [directional-animation, sprite-compatibility, trim, pivot, mirroring, validation, mutation-testing]
problem_type: variant-art compatibility gating and mirrored presentation
---

# Directional Variant Art: Mirror-as-Data and the Two-Tier Compatibility Gate

Patterns from TASK-190 phase E (dev commit `20a8e15`, 2026-08-26). The host-side
`ue-character-profile-directional-animation-patterns.md` owns the overall directional
architecture; this doc captures the compatibility-tier and mirror decisions that changed it.

## 1. Blocking compatibility is the trim-invariant frame space, nothing more

When variant art shares ALL of its gameplay data with a base animation (hitboxes, sockets,
root motion, Cues authored once on the base), the correct **blocking** compatibility set is
exactly the fields that survive independent trimming:

| Field | Read as |
|---|---|
| Untrimmed canvas | `IsTrimmedInSourceImage() ? GetSourceImageDimensionBeforeTrimming() : GetSourceSize()` |
| Pivot in untrimmed source | `GetPivotPosition() - GetSourceUV() + trimOrigin` (trimOrigin = `GetOriginInSourceImageBeforeTrimming()` when trimmed, else zero) |
| Pixels-per-unreal-unit | `GetPixelsPerUnrealUnit()` |
| Source rotation | `IsRotatedInSourceImage()` |

These four are what keep base-owned geometry spatially correct on every facing. Everything
derived from the art's *alpha* — trim origin/size, render-bounds origin/size — legitimately
differs between facings and between independently trimmed exports of the SAME cells.
Blocking on those silhouette fields rejected the Bulk Extractor's own trimmed output: the
gate refused art produced by the plugin's own pipeline. Silhouette drift is now an advisory
validation **Warning** ("verify visually rather than re-exporting"), emitted only when the
frame space itself is clean, first differing field per key frame.

The comparator is split into two named functions (`CompareDirectionalFrameSpace` blocking,
`DescribeDirectionalSilhouetteDelta` advisory returning true-when-different) so the
assignment gate and the validator cannot drift apart on which tier a field belongs to.

## 2. Silhouette test fixtures must mutate the silhouette ONLY — compensate the pivot

The subtle trap in testing the advisory tier: **a naive trim-rect mutation also moves the
pivot-in-untrimmed-source and trips the blocking tier instead.**

- A real independent trim moves the trim origin and the pixels together, so the pivot's
  untrimmed-canvas position is unchanged. A test that calls `SetTrim` with `origin + (1,0)`
  and nothing else has moved the trim bookkeeping *without* moving the art — the pivot
  formula (`pivot − sourceUV + trimOrigin`) shifts by +1 and the blocking tier fires with
  `PivotInUntrimmedSource`, not the silhouette Warning. Compensate:
  `SetPivotMode(Custom, capturedPivot - (1,0))`.
- Re-initializing a sprite to a different source-region `Dimension` drifts the default
  centered pivot by half the delta. Capture `GetPivotPosition()` before the re-init and pin
  it back with `SetPivotMode(Custom, captured)`.
- `BakedRenderData` mutations (bump one max-X vertex for RenderSize; shift every vertex for
  RenderBoundsOrigin) are silhouette-pure as-is — the blocking tier never reads render data.

The mutation proof for the tier itself is re-adding one silhouette field to the blocking
comparator: the silhouette test must then fail on all three of its asserts (profile no
longer valid, Warning absent, blocking Error present).

## 3. Mirror is data beside the art; applying it is project-owned

The per-slot `bMirrorHorizontally` flag lets five authored facings serve an 8-way set. The
load-bearing decisions:

- **Every read returns the flag beside the art** — the resolver gained a `bool&` out-param
  and the enumeration struct a field — so no caller can get art without the chance to honor
  its presentation. The plugin never mutates render state; the game applies the flip
  (typically actor scale), which keeps base-owned hitboxes and root motion untouched.
- **Re-picking a slot's art preserves the flag** (`SetDirectionalSlot` captures the
  existing record's flag before rewriting the slot). A designer swapping art after
  re-export should not silently lose presentation authoring.
- **Same-value writes are refused as no-ops** at both the asset mutator and the panel seam,
  matching the slot-commit convention, so undo history and dirty state stay honest.
- The editor preview deliberately shows the authored art unflipped (documented in the
  guide); flipping the preview canvases is a possible follow-up, not an accident.
- Failure paths clear the flag along with the other outputs — tests seed it `true` before
  failure-path calls so the clearing is provable.

Behavioral coverage that made the mutation proofs discriminate: resolver returns `true` for
a mirrored slot (not just `false` for unmirrored), the enumeration carries the flag on the
mirrored record among unmirrored siblings, and the flag survives an art re-pick plus a JSON
round-trip. Four independent one-line mutations (tier regression, resolver drop,
enumeration drop, re-pick drop) each turned exactly one test red.

## 4. Pin foreign-parity semantics with a source check, then fix the words, not the sign

The angle-offset sign read as "wrong" in review. Inspecting PaperZD 2.2.4's
`GetDirectionIndexByAngle` showed our math byte-identical
(`(angle + offset + sep/2 + 360) / sep`), so the behavior stayed and only the tooltips,
header comment, and guides changed: *a positive offset buckets the incoming facing as more
clockwise, rotating the sector layout counter-clockwise on screen; sign-compatible with
PaperZD's `DirectionalAngleOffset`.* When a convention is shared with an ecosystem
neighbor, parity is itself the contract — document it explicitly so the next reviewer
checks the neighbor before "fixing" the sign.

## See Also

- Host repo: `docs/solutions/ue-character-profile-directional-animation-patterns.md` (the
  directional architecture this refines; update its compatibility section when it is next
  editable).
- `docs/directional-animations-guide.md` — the designer-facing statement of the same
  contracts.
