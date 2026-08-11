---
title: "Runtime Sprite Offset Architecture"
category: architecture
module: Paper2DPlus
date: 2026-07-10
problem_type: architecture_pattern
component: rendering
severity: high
applies_when:
  - "Changing per-frame SpriteOffset, TrimOffset, Layer placement, facing, or scale conversion"
  - "Rendering Character Layer art in live, editor-preview, or hybrid-composite paths"
tags: [paper2d, sprite-offset, character-layer, runtime-rendering, facing, networking, frame-cues]
---

# Runtime Sprite Offset Architecture

**Status: stability-critical. Verify scaled and mirrored characters visually after changing this path.**

Per-frame alignment is authored in pixels and must agree across the base flipbook, live layer children,
editor composites, thumbnails, and transient hybrid recipes. The shared contract lives in
`Paper2DPlusLayerDraw`; consumers must not invent another pixel/world or pixel/canvas conversion.

## Canonical pixel composition

`Paper2DPlusLayerDraw::ResolveTotalOffsetPx` combines:

1. the frame's `FSpriteExtractionInfo::SpriteOffset + TrimOffset`; and
2. the Character Layer placement for that animation (`AnimationOffsets`, otherwise `DefaultOffsetPx`).

The base sprite passes no layer and receives alignment only. Live children, editor surfaces, and hybrid
recipes pass their layer and receive both terms. Offsets remain pixel-space in caches/recipes so the
current sprite PPU, scale, and facing are applied at the final rendering boundary.

## Pixel-to-world conversion

Use `Paper2DPlusLayerDraw::PixelOffsetToWorld`:

```text
x = (pixel_x / max(PPU, 0.001)) * abs(component_scale_x)
x = facing_left ? -x : x
z = (-pixel_y / max(PPU, 0.001)) * component_scale_z
```

`abs(scale.X)` supplies magnitude; facing contributes the **one** X sign. This keeps yaw-flipped and
negative-scale characters consistent with hitboxes and root motion. Screen-down pixel Y becomes world-up
negative Z. Read PPU from the current rendered sprite and never mutate a shared sprite pivot at runtime.

## Apply deltas, not absolute locations

The base profile component and each live layer child retain their last applied pixel/world offset. On a frame
change, convert both old and new offsets using the current basis and apply only the delta with
`AddWorldOffset`. On flipbook/layer teardown, subtract or clear the tracked offset before changing identity.
Do not use `SetRelativeLocation`: parent scale can double-apply the authored movement.

For children, keeping the tracker in pixel space is important. When facing changes, the child's transform has
already mirrored the applied vector; converting old and new pixels under the current basis avoids a permanent
double correction.

## Network-authority exception

Visual children are cosmetic and apply offsets on proxies. The base flipbook is also cosmetic when it is not
the actor root. If the base flipbook **is** the root component, however, `AddWorldOffset` moves the actor and can
fight replicated movement; that proxy-and-root case obeys the same authority gate as root motion. The apply,
cached-world invalidation, and last-applied record must remain in one branch so teardown never subtracts a
phantom delta.

## Late assignment and Frame Cues

Character Profiles and flipbooks may be assigned after `BeginPlay` (including PaperZD-driven playback). The
profile component binds the event-capable Paper2DPlus flipbook component when available and retains the bounded
slow-poll fallback for stock components. Late discovery must warm frame data and resume root motion, Frame Cue
delivery, and offsets together; never fix only the visual path.

## Current owners

- `Public/Private/Paper2DPlusLayerDraw.*` — shared pixel composition and world conversion.
- `Paper2DPlusCharacterProfileComponent.cpp` — base-sprite delta application and root authority gate.
- `Paper2DPlusLayerRenderComponent.cpp` — per-child live application and hybrid recipe consumption.
- Editor layer canvases/thumbnails — `ResolveTotalOffsetPx * zoom` through their existing paint transform.

## See Also

- [Slate editor API patterns](../../../../docs/solutions/ue-slate-editor-api-patterns.md)
- [Layered wardrobe compose seams](../../../../docs/solutions/ue-layered-wardrobe-compose-seam-patterns.md)
- [Paper2DPlus network authority](../../../../docs/solutions/ue-paper2dplus-network-authority-patterns.md)
- [Transient Paper2D compositor](../../../../docs/solutions/ue-transient-paper2d-compositor-patterns.md)
