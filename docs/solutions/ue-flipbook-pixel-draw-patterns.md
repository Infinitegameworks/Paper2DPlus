---
title: "UE Flipbook Pixel-Draw Patterns"
category: editor-patterns
module: Paper2DPlusEditor
date: 2026-07-10
problem_type: best_practice
component: tooling
severity: medium
applies_when:
  - "Editing a PaperFlipbook source-texture subregion in an Unreal asset editor"
  - "Implementing pixel undo, live texture-region preview, or a floating editor verification path"
tags: [paper2d, pixel-editor, texture-source, undo, slate, render-thread, automation]
---

# UE Flipbook Pixel-Draw Patterns

Reusable patterns from building the **Flipbook Draw Tool** — an in-editor Aseprite-style pixel editor
that opens a `UPaperFlipbook` and paints directly onto its frames' source textures (TASK-69, plan
`docs/plans/2026-06-08-001-feat-flipbook-draw-tool-plan.md`). The tool is editor-only
(`Paper2DPlusEditor`), all behind `WITH_EDITOR`.

The whole feature is ~90% wiring of existing seams; the genuinely new/risky parts are the **windowed
texture write**, **pixel undo**, and **live GPU preview**. These patterns capture what wasn't obvious.

---

## 1. Windowed in-place texture-source write (don't re-`Init` the sheet)

A `UPaperFlipbook` key frame is a `UPaperSprite` that views a **sub-rectangle** (`GetSourceUV()` +
`GetSourceSize()`) of a (usually shared) source `UTexture2D`. Painting "a frame" means editing only
that window. Every existing write-back in the codebase (`AsepriteReimporter.cpp`,
`SpriteExtractionUtils::PadTextureInPlace`) calls `Source.Init(W,H,...)` then memcpy's the **whole**
texture — correct for reimport, **wrong for a brush** (it reallocates the mip and would wipe sibling
frames packed on the same sheet).

The brush write (`FFlipbookPixelEdit::WriteFrame`) instead:
- `LockMip(0)` **without** `Init`,
- memcpy's only the sprite's `[SourceUV, SourceUV+SourceSize)` rows,
- `UnlockMip(0)` → `UpdateResource()` → `MarkPackageDirty()` once.

```cpp
FTextureSource& Source = Texture->Source;
if (Source.GetFormat() != TSF_BGRA8) return false;          // fail-closed (see §6)
const int32 TexW = Source.GetSizeX();
const int32 UVx = FMath::RoundToInt(Sprite->GetSourceUV().X);
const int32 UVy = FMath::RoundToInt(Sprite->GetSourceUV().Y);
if (UVx < 0 || UVy < 0 || UVx + W > TexW || UVy + H > Source.GetSizeY()) return false; // bounds BEFORE lock
uint8* Dest = Source.LockMip(0);
for (int32 Y = 0; Y < H; ++Y)
    FMemory::Memcpy(Dest + ((int64)(UVy+Y)*TexW + UVx)*4, SrcBytes + (int64)Y*W*4, (int64)W*4);
Source.UnlockMip(0);
Texture->UpdateResource(); Texture->MarkPackageDirty();
```

**`FColor` bytes are B,G,R,A in memory — identical to `TSF_BGRA8` source byte order** — so an
`FColor[]` buffer memcpy's straight in/out with no per-channel swizzle. (`ReadSpriteSourceRegion`
reconstructs `FColor(R,G,B,A)` from the BGRA bytes, so read→edit→write round-trips exactly.) A
**sibling-frame-isolation** unit test (paint frame 0 red on a 2-frame shared sheet, assert frame 1's
bytes unchanged) is the guardrail — keep it.

## 2. Pixel undo is your OWN snapshot stack (transactions don't capture mip bytes)

`FTextureSource`'s bulk bytes are serialized by a custom `Serialize`, **not** a reflected `UPROPERTY`.
So `FScopedTransaction` + `Object->Modify()` will **not** roll back a brush stroke (the only existing
pixel rollback in the repo is the file-based `SnapshotTexture` for auto-pad). Maintain an in-memory
undo/redo stack of `{Frame, DirtyRect, Before[], After[]}` (sub-rect pixels only), exactly like the
editor's `QueueUndoStack`. Snapshot the buffer at stroke start; on commit, extract the before/after
sub-rects for the accumulated dirty rect. `Undo`/`Redo` re-route through `SetCurrentFrame(Entry.Frame)`
first so the edit always lands on the frame it came from (a frame switch reloads the working buffer
from the committed source; same-frame is a no-op so the in-memory buffer is reused).

## 3. Live paint without GPU thrash — `UpdateTextureRegions` during the drag, commit on mouse-up

`UpdateResource()` rebuilds the platform texture — far too heavy per brush dab. Pattern:
- Keep a CPU **working buffer** (`TArray<FColor>`) for the current frame and a **transient mirror**
  `UTexture2D::CreateTransient(W,H,PF_B8G8R8A8, NAME_None, ImageDataView)` (set `Filter=TF_Nearest`,
  `SRGB=true` to match the source; the canvas draws THIS texture, full 0..1 UV).
- During the stroke, stamp into the buffer and push only the dirtied sub-rect to the render resource via
  `UTexture2D::UpdateTextureRegions(0, 1, Region, SrcPitch, 4, Temp, CleanupFn)` —
  `FUpdateTextureRegion2D(DestX,DestY,SrcX,SrcY,W,H)` (in `RHITypes.h`). **`SrcData` must outlive the
  render-thread copy** → malloc a tight copy of the sub-rect and `FMemory::Free` it (plus `delete` the
  region) in the `DataCleanupFunc`.
- On mouse-up, commit the buffer to the real sprite source with the §1 windowed write (one
  `UpdateResource()`).

Drawing the transient mirror (not the sprite) during editing also sidesteps the **`GetBakedTexture()`
vs `GetSourceTexture()`** divergence — the canvas always shows exactly the buffer you're editing.

## 4. Paint-then-hit-cache: derive mouse→pixel from the SAME transform you paint with

A pixel canvas must map a screen click to an exact integer texture pixel and back. Adopt
`SSpriteExtractorCanvas`'s free-zoom model (raw `ZoomLevel` + `PanOffset`, **zoom toward the cursor**),
not the auto-fit canvases (which recompute zoom from widget size every frame and shift the pixel grid on
resize). Do a one-time **fit-on-open** in the first `OnPaint` once geometry is known. Compute
`DrawPos = PanOffset + (WidgetSize - FrameSize*Zoom)*0.5`, then `FramePixel = (Local - DrawPos)/Zoom`,
and paint at the same `DrawPos/DrawSize` — so the brush can never drift from what's drawn (mirrors how
`SLayerPreviewCanvas` builds its hit cache from `DrawFlipbookSprite`'s `OutDrawPos/OutDrawSize`).
Mandatory: host the `SLeafWidget` in a parent with `.Clipping(EWidgetClipping::ClipToBounds)` (and
`SetClipping` in `Construct`), bind pan to middle/right mouse (left = the tool), and make the canvas an
**`FGCObject`** that roots the transient working `UTexture2D` (it's otherwise unreferenced and GC can
reclaim it mid-edit).

## 5. Launch a custom editor for an ENGINE-owned asset type via the right-click menu

You **cannot** register a second `IAssetTypeActions` for `UPaperFlipbook` to override its double-click
editor — Paper2D already owns it; registration is additive and last-wins is undefined. The idiomatic,
already-proven hook is a Content-Browser context-menu entry:

```cpp
UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("ContentBrowser.AssetContextMenu.PaperFlipbook");
// ... AddMenuEntry with FExecuteAction that does:
for (UPaperFlipbook* FB : Context->LoadSelectedObjects<UPaperFlipbook>())
    MakeShared<FFlipbookDrawEditorToolkit>()->InitEditor(EToolkitMode::Standalone, nullptr, FB);
```

`InitAssetEditor(..., InFlipbook)` registers the asset with `UAssetEditorSubsystem` (save/dirty/
single-instance) and GC-roots it; the locally-made `TSharedRef` toolkit is retained by the subsystem.
A brand-new editor over a bare engine type should use its **own lightweight model**, not the coupled
`FCharacterProfileEditorModel` (which is wedded to profile/layer assets).

The discoverable route is **Content Browser → right-click a Paper Flipbook → Common → Paper2D+
Actions → Edit Frames (Paper2D+ Draw)…**. Put related plugin commands in that one submenu instead of
scattering loose rows through the asset-specific section. The submenu and each command use distinct
plugin-owned SVG brushes registered by `FPaper2DPlusEditorStyle`, so the grouping remains recognizable
without reusing the engine's generic Paper Flipbook icon for unrelated actions.

## 6. Non-BGRA8 sources fail closed (and the format guard belongs at read time)

`ReadSpriteSourceRegion`/`WriteFrame` only handle `TSF_BGRA8`. A frame whose source is another format
loads as **not drawable** — the canvas falls back to a read-only sprite draw + a banner, never a
silent no-op or a corrupting write. (Convert-to-BGRA8-on-confirm is a documented follow-up.)

## 7. UE 5.7 console-command + pure-core gotchas

- **`FAutoConsoleCommandWithArgs` does not exist in 5.7.** For an args-only command use
  `FAutoConsoleCommandWithWorldAndArgs` (+ `FConsoleCommandWithWorldAndArgsDelegate`) and ignore the
  `UWorld*` param. (Other variants: `WithWorld`, `WithArgsAndOutputDevice`, …)
- **`AddReferencedObject(UObject*)` is `C4996`-deprecated** — pass a `TObjectPtr<>` overload, or the
  forced-unity `-WarningsAsErrors` gate fails.
- Keep brush math (`StampDab`, `StampLine`, `StampRectangle`, `FloodFill`, mirror) and whole-frame
  pixel transforms in a **pure, model-independent** core
  (`FFlipbookPixelEdit`) taking plain `bool bMirrorX/bMirrorY` (not the editor enum) so it's
  worldless-unit-testable. Flood fill is a bounded stack fill (each filled pixel != target, so it's
  never re-pushed). Dimension-preserving transforms return `false` for invalid input and real no-ops,
  which lets the canvas avoid empty history entries. Build sprites in tests via
  `CreatePerLayerSpriteSheetTexture` + `CreateSpritesFromSheet`.

## 8. Verifying a floating custom asset editor

The draw editor opens as a separate floating top-level window, which gameplay-only screenshot helpers cannot
capture. A live bridge can find widgets across all top-level windows, inject input, and read a durable probe;
the unattended release path should use the bounded headless-tour/watchdog pattern in See Also.

1. **Open it via a console command** (`Paper2DPlus.OpenDrawEditor`) — `open_asset_editor` would open
   the stock Paper2D flipbook editor, not yours. Add a tiny `FAutoConsoleCommandWithWorldAndArgs`.
2. `find_slate_widgets {type_name:"SFlipbookDrawCanvas", all_windows:true}` — `all_windows` is
   load-bearing (the default walk misses floating windows); confirms the canvas realized + returns its
   Slate-absolute center/size.
3. `simulate_slate_drag` across the canvas — `from_leaf`/`to_leaf == SFlipbookDrawCanvas` +
   `down_handled`/`up_handled` proves the canvas handled the stroke.
4. **Read back a pixel** with a probe console command (`Paper2DPlus.DrawProbe` → `UE_LOG`, then grep
   the editor log) — proves the brush actually wrote to the source. This substitutes for a screenshot.
5. `simulate_slate_key` for `Ctrl+Z` (undo), `SpaceBar` (playback), `B/E/I/G/L/R` (tool hotkeys) — focus
   the canvas via `focus_x/focus_y` first.

**Gotcha:** adding a side panel shifts the canvas's center — **re-run `find_slate_widgets` after any
layout change** and click the returned center, don't reuse a stale coordinate.

## 9. Separate drawing symmetry from whole-frame transforms

“Mirror” is ambiguous in a paint program: it can mean **reflect every new brush mark while drawing**
or **flip the pixels already in the sprite**. Present these as separate groups:

- **Drawing Symmetry** — persistent Left ↔ Right and Top ↔ Bottom toggles. Tooltips explicitly say
  they affect new strokes/shapes only. Pencil, Eraser, Line, and Rectangle all honor the toggles.
- **Frame Transform** — one-shot Flip Left ↔ Right, Flip Top ↔ Bottom, Rotate 180°, one-pixel nudges,
  and Clear. These operate on the current frame's existing pixels and are each undoable.

Line and Rectangle are drag-preview tools. Snapshot the working buffer on mouse-down; on every move,
restore that snapshot before stamping the new candidate shape. Push the **union of the previous and new
preview dirty rectangles** to the transient texture so pixels from the old preview are visibly erased.
Only the final shape dirty rectangle is committed on mouse-up, producing one undo entry instead of one
entry per preview update.

Frame transforms go through the same working-buffer commit funnel as strokes. They preserve the frame's
W×H window, push the full frame to the transient preview, call the windowed source write once, clear redo
only after a successful change, and restore the before-buffer if the write fails. One-pixel nudges discard
pixels crossing an edge and expose transparent pixels on the opposite edge. A 180° rotation is safe for
every rectangular frame; quarter turns are intentionally absent because swapping W/H would require a
separate sprite-window/texture-layout workflow rather than silently cropping or resampling pixel art.
Because transform buttons live outside the focusable canvas, return keyboard focus to the canvas after a
click so pixel-specific Ctrl+Z/Ctrl+Y works immediately. If an undo/redo source write fails, restore the
working buffer and keep the history entry on its original stack instead of reporting progress that was not
persisted.

## 10. A texture write must re-derive the sprite geometry that was computed FROM those pixels

**The bug (2026-07-27):** frames edited in the draw tool rendered visibly thinner and shifted, with part
of the character sliced off. It survived reopening the asset. It appeared to fix itself if you changed a
sprite property and changed it straight back — which is what makes the cause legible.

A `UPaperSprite`'s render and collision geometry is not necessarily authored. Three of the five
`ESpritePolygonMode` values compute it **from the source alpha**:

| Mode | Pixel-derived? |
|---|---|
| `SourceBoundingBox` | no — from `SourceUV` / `SourceDimension` |
| `TightBoundingBox` | **yes** — `FindTextureBoundingBox` scans alpha. *This is the engine's CDO default for every sprite's `CollisionGeometry`* (`PaperSprite.cpp`, ctor) |
| `ShrinkWrapped` | **yes** — contour trace over alpha |
| `FullyCustom` | no — hand-authored |
| `Diced` | **yes** — dices `BakedRenderData` against an alpha bitmap |

That derived result is then **serialized onto the sprite**, in a different package from the texture. And
nothing invalidates it when the texture's pixels change: `UPaperSprite::PostLoad` rebuilds only on asset
*version* upgrades, and `UTexture2D::UpdateResource()` / `MarkPackageDirty()` notify no dependent sprite.
So a windowed write that moves or resizes the art silently desynchronises geometry from pixels, and the
stale bounds persist on disk.

**The rule:** any code path that writes texture pixels must re-derive the geometry of every sprite that
views the written region, in the same commit:

```cpp
Texture->UpdateResource();
Texture->MarkPackageDirty();

Sprite->RebuildData();     // exactly what PostEditChangeProperty does; no-op in shape for
                           // SourceBoundingBox / FullyCustom, which never read pixels
Sprite->MarkPackageDirty(); // bounds live on the SPRITE package, not the texture's
```

Put it in the **write funnel**, not at the call sites. `FFlipbookPixelEdit::WriteFrame` is reached by
stroke commits, whole-frame transforms, *and* undo/redo apply — hoisting the rebuild into the funnel means
an undo restores pixels and bounds together instead of restoring pixels under a stale box.

**Why the round-trip "fix" worked, and why that is the diagnostic:** editing any property fires
`PostEditChangeProperty` → `RebuildData()`, which re-scans the current pixels. Setting the value back fires
it a second time. The value never mattered; the *edit* did. Whenever "change it and change it back" repairs
something permanently, suspect a serialized cache with no invalidation path rather than a bad value.

**Cost:** `FindTextureBoundingBox` only scans the sprite's own cell, but it builds an `FAlphaBitmap` over
the whole sheet first — a few ms on a large atlas, paid once per stroke/transform/undo step (mouse-up), not
per pixel. The engine's own sprite editor pays the same on every property tweak.

**Verifying it:** `FindTextureBoundingBox` is public, so a worldless test can use it as an oracle — write a
buffer whose art moved *and* changed width, then assert the stored box matches the oracle, plus a
`TestNotEqual` against the pre-write box so the assertion cannot pass vacuously. `RenderGeometry` /
`CollisionGeometry` are protected but are `UPROPERTY`s, so a test reaches them via
`FindPropertyByName` + `ContainerPtrToValuePtr`. Note `FMath::RoundToInt(double)` returns **`int64`** under
LWC, so cast pixel values to `int32` or `TestEqual` is ambiguous (C2666) across 5.0–5.8.

## See Also

- [Character Profile editor tabs](../../../../docs/solutions/ue-character-profile-editor-tab-architecture.md)
- [Slate editor API patterns](../../../../docs/solutions/ue-slate-editor-api-patterns.md)
- [Cross-sheet alignment](../../../../docs/solutions/ue-cross-sheet-alignment-patterns.md)
- [Paper2D key-frame vs timeline frame](../../../../docs/solutions/ue-paper2d-keyframe-vs-timeline-frame.md)
- [Worldless automation tests](../../../../docs/solutions/ue-worldless-automation-test-patterns.md)
- [Headless editor screenshot tours](../../../../docs/solutions/ue-headless-editor-screenshot-tour-patterns.md)
- [Slate drag and headless verification](../../../../docs/solutions/ue-slate-drag-reorder-and-headless-verification.md)
