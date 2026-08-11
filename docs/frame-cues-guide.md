# Frame Cues: Designer and Blueprint Guide

Frame Cues attach reusable data to definitive Paper2D key frames, and a Cue Type may also carry its own behavior. When a cue fires, its own overridable events run first and the Character Profile Component then broadcasts the same notification to every listener, so a Blueprint or C++ receiver can still decide what else happens.

> **This reverses the earlier "Frame Cues are data, receivers own behavior" rule, deliberately.** The data-only rule was a user-approved decision, and reversing it in v8.0.0 was another one: the old rule cost the notify-style workflow designers actually wanted, and the six built-in Cue types existed largely to paper over the gap -- which is why v8.0.0 also deletes them outright. This is not drift and not an oversight -- do not "restore" the data-only rule in code or in docs. The envelope described below is what keeps a Cue Type bounded now.

## Cues and Cue States

| Cue kind | Timing | Lifecycle |
|---|---|---|
| Cue | One `TriggerFrame` | `Triggered` once when traversal crosses the anchor |
| Cue State | `StartFrame` plus `FrameCount` | `Began` on entry, optional `Updated` while active, and `Ended` on exit or forced teardown |

Cue State spans are half-open: a state starting at frame 3 with a count of 2 contains key frames 3 and 4. The editor keeps new and moved placements inside the selected animation.

In C++, `UPaper2DPlusCue` and `UPaper2DPlusCueState` derive from the hidden abstract
`UPaper2DPlusCueBase`. Content saved with the retired `UPaper2DPlusMomentCue`,
`UPaper2DPlusRangeCue`, or `UPaper2DPlusFrameCue` names loads through direct plugin-local
CoreRedirects. Update native and Blueprint type references to the final names; redirects preserve saved
content, not source-level symbols.

## What happens when a Cue fires

Every notification runs the same two steps, in this order, at every site -- ordinary frame changes, forced teardown, the equipment sweep, and editor preview alike:

1. **the cue acts** -- the placement's own behavior event runs;
2. **the world reacts** -- the Character Profile Component broadcasts `OnFrameCue` to every listener; in the editor, the isolated preview world already reflects the behavior and the host then notifies any optional adapters.

| Cue kind | Overridable behavior events |
|---|---|
| Cue | **On Cue Triggered** |
| Cue State | **On Cue Begin**, **On Cue Update** (only while `Emit Updates` is on), **On Cue End** |

They are ordinary Blueprint-implementable events on the Cue classes with empty native default bodies, so a Cue Type that implements none of them behaves exactly as it did before v8.0.0. You author them in the **Frame Cue Type** editor's one permitted event graph, and a newly created type is pre-seeded with the events for its kind -- On Cue Triggered for a Cue, On Cue Begin and On Cue End for a Cue State, with Update left opt-in.

### Behavior must be stateless

A placement is a **shared, asset-owned object**. Two characters playing the same animation -- and one character dispatched through two components -- run the behavior on the *same* cue instance. Behavior must therefore read only that placement's payload and the notification Context, act on the world, and **never write instance state onto the cue**. A member variable used as "am I active?" or "the effect I spawned" is one character's playback leaking into another's.

Keep anything that needs releasing on the receiving actor or component: spawn it in **On Cue Begin**, remember the handle on the actor, and release it in **On Cue End** or in your listener. Anything a cue spawns into the world should own its own end; never store its handle on the cue. Cue behavior itself is synchronous, so delayed cleanup belongs to the spawned actor/component or another receiver-owned system, not to a Delay, timer, or async node in the Cue graph.

### Use the live invocation context for spatial behavior

Every behavior and listener callback receives one coherent `FPaper2DPlusFrameCueContext` snapshot. In addition to the existing actor, Profile Component, flipbook, animation, frame, phase, network, and End fields, it directly exposes:

- `CharacterProfile` -- the exact base/compiled Character Profile used for this invocation;
- `PlaybackComponent` -- the live `UPaperFlipbookComponent` that renders the timeline and can be an attachment parent;
- `EvaluationMode` -- why this invocation is being evaluated.

There is deliberately no PaperZD AnimInstance field. The live Paper component is the spatial authority in a standalone Paper2D game, a PaperZD-driven game, and the isolated editor preview.

| Evaluation mode | Meaning | Compatible legacy flags |
|---|---|---|
| `RuntimePlayback` | Ordinary game playback; also the safe default for a default-constructed legacy Context | `Is Editor Preview = false`, `Is Catch Up = false` |
| `RuntimeCatchUp` | Replicated/corrective catch-up that rebuilds eligible state without replaying one-shot Cues | `Is Editor Preview = false`, `Is Catch Up = true` |
| `EditorPlayback` | The Frame Cues tab is playing its preview timeline | `Is Editor Preview = true`, `Is Catch Up = false` |
| `EditorScrubSeek` | A frame-strip/timeline seek or scrub evaluation | `Is Editor Preview = true`, `Is Catch Up = false` |
| `EditorSelectionPreview` | **Preview Selected** is inspecting one placement | `Is Editor Preview = true`, `Is Catch Up = false` |

Use **Resolve Frame Cue Anchor** (`UPaper2DPlusBlueprintLibrary::ResolveFrameCueAnchor`) rather than rebuilding pivot/facing math in the Cue Blueprint:

1. Pass the callback Context, choose **Render Origin** or **Profile Socket**, and supply a socket name only for Profile Socket.
2. Branch on the returned `EPaper2DPlusFrameCueAnchorResult`.
3. On `Success`, spawn at `OutWorldTransform`.
4. For a detached spawn, keep that world transform. To follow the character, attach to `OutAttachmentComponent` with **Keep World** location, rotation, and scale.

The returned Profile socket name is **not** an Unreal scene-component socket. Do not pass it to Attach and do not use Snap to Target: the resolver has already converted the authored Profile socket into the returned world transform. A Profile Socket changes the component transform's translation to the canonical socket position while retaining component rotation and scale exactly once; do not reapply Sprite Offset, trim offset, Layer placement, facing, or scale.

| Anchor | Data used |
|---|---|
| `RenderOrigin` | The live playback component's component transform. It does not require a Character Profile. |
| `ProfileSocket` | The unique **base/compiled Character Profile** row matching both `Context.AnimationName` and `Context.Flipbook`, at `Context.CurrentFrame` (or the outgoing `PreviousFrame` for an End whose current frame is unset). It uses the shared runtime/editor pivot, facing, and nonuniform-scale geometry. |

That Profile Socket meaning is a permanent compatibility boundary. A Cue composed from a Runtime Customizable Layer still receives the base timeline playback component and may resolve a base/compiled Profile socket. Layer-child/effective sockets and Layer provenance are not part of this anchor kind; a Layer-only socket fails instead of silently choosing another component.

Every failure resets the world transform to identity and the attachment component to null. The exact results are:

| Result | Meaning |
|---|---|
| `Success` | The world transform and attachment component are valid. |
| `InvalidAnchorKind` | The enum value is unsupported. |
| `InvalidPlaybackComponent`, `PlaybackComponentUnregistered` | The Context has no usable live render source. |
| `InvalidOwner`, `OwnerMismatch`, `WorldMismatch` | The component, actor, Profile Component, and world do not describe one coherent invocation. |
| `MissingProfile`, `MissingFlipbook`, `MissingAnimation` | Profile Socket lacks required snapshot identity. |
| `ProfileRowNotFound`, `ProfileRowAmbiguous` | Animation name plus flipbook does not identify exactly one base Profile row. |
| `FrameOutOfRange` | The Context frame is not valid for that row. |
| `MissingSocketName`, `SocketNotFound`, `SocketAmbiguous` | The requested case-insensitive socket name is empty, absent, or duplicated by case. |
| `PivotUnavailable`, `NonFiniteGeometry` | Canonical frame geometry cannot produce a finite transform. |

A Profile Socket failure never falls back to Render Origin. Handle the result explicitly so a missing socket cannot spawn an effect in a plausible but wrong place.

### Every Begin gets an End -- with one exception

An active Cue State receives its paired End at every teardown boundary: ordinary exit from the range, animation change, `EndPlay` / component destruction, the equipment (wardrobe) sweep, and the stale sweep.

The one exception is deliberate. If the Cue Type **asset** is deleted while one of its ranges is active, listeners still receive the End -- the placement object is alive and may hold receiver-owned state -- but the cue's **On Cue End behavior does not run**, because a placement whose class is gone can neither run behavior nor serve payload. That asymmetry is exactly why releases belong on the receiver: a receiver-owned release always happens, a behavior-owned one can be skipped in this one case.

A loop wrap is deliberately **not** one of those boundaries. A range that still covers the frame playback wrapped to stays active across the seam: it receives no End and no second Begin, only its ordinary **On Cue Update** if `Emit Updates` is on. A Cue State authored across a whole looping animation is therefore one continuous range for as long as the loop runs, never an End/Begin pair every cycle -- which is what keeps a range-scoped receiver effect, such as a gameplay tag applied on Begin and released on End, from flickering off and back on at every wrap. Only a range that does **not** cover the post-wrap frame ends there, and its End Reason is `LoopReset`.

Two more end-shapes are worth knowing:

- The equipment sweep that runs when a Layer is equipped or removed ends only the ranges that left the composed set. A range that is still present and still in range receives an **On Cue Update** from that sweep even though no frame changed, if its Cue Type has `Emit Updates` on. Read Update as "still active", never as "one frame has passed".
- At a loop wrap, a still-active range whose Cue Type asset has been deleted ends with `SourceRemoved` rather than `LoopReset`, because the same call's stale sweep reaches it first.

## Place a Cue on an animation

1. Open the Character Profile and choose an animation from the searchable animation picker.
2. Open the **Frame Cues** tool. Its unified timeline is always visible for the selected animation, even when no Cue or curve has been authored yet.
3. Move to the intended definitive key frame.
4. Use the implicit **Default** track, or open **Manage tracks...** to create a named track and point **+ Add Cue** at it when organization will help.
5. Choose **+ Add Cue** in the timeline's top bar.
6. Search the project's reusable Cue Types -- the built-in **Spawn Flipbook Cue** is always available. Choose **+ New Frame Cue Type...** when the payload shape does not exist yet.
7. Select the placement directly on the timeline and edit its payload in **Details**. Timing is edited on the timeline so the placement and details cannot disagree.
8. Drag the Cue to move it. Drag a Cue State edge or use Shift+Arrow to resize it. Drag a Cue's **anchor diamond** to either side of its cell to choose the **trigger edge**: on the left boundary the cue fires when the frame starts (the default); on the right boundary it fires when the frame **ends** -- advancing to the next frame, looping past it, or the animation naturally completing on it. A frame cut short (manual stop, animation switch) never fires its end-anchored cues, because the frame never finished.
9. Use **Preview Selected** to run the selected Cue's real behavior in the isolated editor preview world. Choose **Clear** to deliver any pending Cue State End and remove everything the preview spawned.

There is no indexed Cue list to keep in sync. Direct timeline selection drives one contextual **Details** panel, while **Preview** remains the separate preview surface. Reordering, duplicate/delete, undo/redo, panel close/reopen, and animation changes re-resolve the selected Cue's stable identity instead of writing through a remembered array index.

Timeline placements carry a readable visual identity without requiring extra authoring. A fresh Cue Type starts with the replaceable `Paper2DPlus.Cue.Default` tag, whose built-in convention color is used while **Color** remains white. An authored non-white Color always wins; otherwise an exact or ancestor entry in **Project Settings → Paper2DPlus → Tag Colors** overrides the convention. **Debug Name** is the label when present and the Cue Type display name is the fallback. A Cue paints its anchor diamond on the boundary it fires on, while a Cue State paints an inset span, so a one-frame state is still distinct from an instant. Behavior-error badges compose above that identity, and screen-reader summaries retain the same label, timing form, frame/duration, and error.

### Timeline keyboard controls

Every animation has an implicit **Default** Cue track. Optional named tracks are organization only: you may keep every Cue on Default or split Cues across as many named tracks as are useful. Moving a Cue between tracks never changes its frame timing or runtime dispatch order. The top bar owns **+ Add Cue** and **+ Add Curve**; the persistent **+ Add Track** button is retired, and every track command -- create, rename, reorder, remove, and choosing which track **+ Add Cue** targets -- now lives behind one **Manage tracks...** overflow. Choosing the Add Cue target selects that track, which clears the current Cue selection exactly as clicking a track header does.

Profile-owned curves appear inline beneath the Cue tracks on the same frame axis. The bounded left gutter is shared by the frame strip, Cue tracks, and curve legend; drag its divider to resize labels while keeping every timing column aligned. Curve legend rows show only color and name. Select a curve on the timeline to use **Details** for graph visibility, solo, interpolation mode, rename, orphan-key repair, and removal, then edit keys in the shared engine curve editor. There is no separate Curves tab or Curves details category. From Layer scope the curves remain visible and selectable but their mutation controls fail closed because the Character Profile owns them.

With a Cue selected and the timeline focused:

- Left / Right: move the placement by one key frame.
- Shift+Left / Shift+Right: resize a Cue State.
- Ctrl+D: duplicate the selected placement.
- Delete or Backspace: remove the selected placement.

Right-click a Cue for its timing, duplicate, and delete actions.

## One built-in Cue type: Spawn Flipbook Cue

**Paper2DPlus ships exactly one concrete runtime Cue class — the Spawn Flipbook Cue — and every other placeable Cue is a Cue Type you author.** The plugin provides the two abstract designer parents (**Cue** and **Cue State**), the hidden common base, the dispatch machinery, the restricted Cue Type editor, and the events -- not a catalogue of ready-made cues. Editor-only `HideDropdown` automation/visual-tour fixtures are excluded from the picker and cooked data.

**Spawn Flipbook Cue** (`UPaper2DPlusSpawnFlipbookCue`) spawns a one-shot visual flipbook effect when its frame is crossed: a soft Flipbook reference (warmed before the animation plays, never loaded on a dedicated server), an Offset in character-local units, Rotation, Scale, Tint, Play Rate, flip-with-character, an optional attach-to-character mode, and a Render Origin or Profile Socket anchor. The spawned component destroys itself when playback finishes; looping is forced off because a looping effect would never release itself. Its class default Net Policy is **Cosmetic Only** and is frozen. Its unique editor affordance: **with the placement selected in the Frame Cues tool, drag its ghost directly in the preview viewport** — the standard translate gizmo moves the authored Offset, one drag is one undo entry, and the effect re-previews where you drop it. Subclass it as a Cue Type if you need extra payload or different behavior; the gizmo follows subclasses too.

The Cue Type editor uses Unreal's stock **My Blueprint** panel for ordinary payload variables. Use its adjacent **Override** control to implement `OnCueTriggered` or the Cue State lifecycle events; that control deliberately routes through Paper2DPlus's safe event-node path so a designer-authored parent implementation is called. Arbitrary function creation remains disabled. A quarantined legacy Cue Blueprint shows recovery guidance instead of My Blueprint, preserving its forbidden graphs without exposing them for editing or deletion.

The familiar Blueprint toolbar is present too: **Compile**, **Diff**, **Find**, **Hide Unrelated**, **Class Defaults**, and PIE/debug controls all work in the restricted mode, and Find opens a real **Find Results** tab. Compile still runs Paper2DPlus's protected Cue validation. **Class Settings**, **Delete Unused Variables**, and automatic save-on-compile options are deliberately absent because they can escape the Cue Type envelope or save before its durable schema transaction finishes.

New Cue Type creation writes the fresh identity and the Cosmetic Only networking policy onto that generated Cue Type's class defaults only. It does not tag the abstract native Cue bases, seed the project's Tag Colors settings, or modify Effect Profiles. Replace the default tag or choose an authored Color whenever the cue needs a different identity.

> **BREAKING in v8.0.0 -- the six built-in Cue types were deleted.** Play Sound Cue, Spawn Effect Cue, Spawn Projectile Cue, Camera Shake Cue, Screen Flash Cue, and Apply Gameplay Tag Cue are gone. **No migration and no redirect is provided, deliberately.** A redirect to a class that no longer exists would help nobody, and a redirect onto some other Cue class would silently reinterpret one cue's saved payload as another's.
>
> **What this means for an existing project:** every placement of one of those six stops firing. It is not dispatched, it does not broadcast to `OnFrameCue`, and validation flags it as a placement that cannot be delivered. Nothing erases those rows from your assets -- they sit there inert until you replace them.
>
> **What to do:** author a Cue Type for each payload shape you were using and re-place it. The payloads were ordinary properties, so rebuilding one is a matter of adding the same fields (**Add Field** in the restricted editor) and implementing the same event. A Play Sound Cue was a Sound field plus volume/pitch/start-time/attach fields with an **On Cue Triggered** body calling **Play Cue Sound**; a Camera Shake Cue was a shake class plus scale/radii/falloff fields with no body at all, read by an `OnFrameCue` receiver. Nothing about the deleted classes was privileged -- everything they could do, a Cue Type you author can do, which is precisely why they were removed rather than kept as a parallel authoring path.
>
> Also removed with them: the legacy Frame Event -> Frame Cue conversion bridge and the legacy Effects/direct-data migrations (legacy data of those shapes is now inert, and is **not** deleted from your assets), the five bespoke native preview adapters (the adapter framework and its project registration setting remain -- see below), and the Effect-specific validation codes those cues produced.

## Create a reusable Frame Cue Type

Use a **Paper2D+ Frame Cue Type** when several placements need the same payload shape, the same behavior, or both. It is a specialized Blueprint asset with a deliberately narrow envelope; neither creation route asks you to choose an arbitrary parent class.

### Create and place from the timeline

1. Move to the intended key frame and choose **+ Add Cue**, then **+ New Frame Cue Type...**.
2. Choose **Cue** for a one-frame trigger or **Cue State** for a span. Pick the asset name and folder in the guided dialog.
3. The restricted **Frame Cue Type** editor opens with the exact Profile or Layer animation, frame, and placement owner captured. Add payload fields, edit their class defaults, and review compiler results there.
4. Choose **Create and Place**. Paper2DPlus compiles and durably saves the Cue Type first, then creates exactly one placement on the captured frame and selects it for immediate **Details** editing.
5. Canceling creation, closing the editor, declining a destructive schema change, failing compile/save, or invalidating the captured target creates no placement. Fix the asset and retry without receiving a partial Cue.

Existing ready Cue Types appear in the same searchable **+ Add Cue** picker and can be placed directly without recreating them.

A new Cue Type is **placement-ready as soon as it is created**: creation now finishes the durable schema staging and the protected save that placement classification requires, so the type appears in **+ Add Cue** in the same session without a visit to the restricted editor's Save. A type that is *not* placement-ready is no longer hidden -- the picker lists it as a disabled row carrying the reason it cannot be placed, and clicking that row opens it in the restricted **Frame Cue Type** editor so you can fix it. Structural non-assets -- the abstract Cue/Cue State parents, and the skeleton/reinstancing/trash classes a loaded Cue Type drags along -- are still not listed.

After creation, **Save All and ordinary Content Browser saves are the normal workflow**. Compatible payload additions and behavior edits compile and update the durable Cue Type baseline automatically. Autosave writes a self-consistent recovery copy, but it does not make an unsaved Cue Type placement-ready or replace the canonical durable baseline; use Save or Save All for that. A schema edit that can reset existing placement values (for example removing or retyping a variable) still requires an on-screen review, but that review happens at **Compile**: the restricted Frame Cue Type editor lists the exact consequences and asks once. After you confirm, the next successful save — the editor's own Save, Save All, or a Content Browser save — commits the reviewed change. The confirmation is exact and session-local: editing the schema again, or reopening the editor after a restart, re-arms the review. An unreviewed schema change refuses to save from any path, and the refusal reason now appears as an on-screen notification (not only in the log), naming the action that unblocks it.

### Create from the Content Browser

Choose **Add > Paper2D+ Frame Cue Type**, then choose **Cue** or **Cue State** in the same guided kind dialog. This creates a reusable type without a pending placement; place it later from any Profile or Layer **+ Add Cue** picker. The Content Browser calls the factory only once the inline rename commits, so the asset already carries its final name and its first save leaves no redirector behind; that save runs one tick later, after the Content Browser and the opening editor have finished touching the package.

### Author payload fields and defaults

The restricted editor supports Blueprint member-variable payloads and class defaults, including primitives, enums, structs, arrays, sets, maps, object/class references, soft references, and project-defined Blueprint types. Fields must be instance-editable and durably serializable so defaults, placement overrides, duplication, undo, save/reload, and cooking agree.

When behavior needs effect art, add an ordinary scalar **soft Paper Flipbook** variable and use it directly from the Cue's behavior. Add offset, rotation, scale, facing, tint, and other payload as normal variables too. Paper2DPlus does not assign a special Effect-field kind or attach library filters to Cue variables.

### What the Cue Type envelope permits and refuses

A Cue Type may implement its declared behavior events, and nothing else. The permitted set is exactly: **overrides of the declared Cue events**, the single event graph page they live on, and the compiler's own representation of that page (the ubergraph function and its generated frame property).

Everything else is still refused -- at authoring time, at compiled-save time, and at cook time, with the same diagnostics as before:

- **more than one event graph page**;
- **function graphs, macro graphs, and delegate signature graphs** -- an override of a declared Cue event is the only executable graph shape allowed;
- **any generated function** that is not one of the declared Cue events, so a custom event cannot borrow a declared name to smuggle a different signature onto the class;
- **latent calls and delays, async task/action nodes or factories, and deferred timer/self-scheduling calls**;
- **components** and construction scripts, **timelines**, and **implemented Blueprint interfaces**;
- **delegate and multicast-delegate payload fields**;
- payload fields declaring a **Blueprint getter or setter** -- payload access must stay data, not a call;
- **replication and RepNotify** on payload fields, including a replication condition;
- payload fields that are **transient, config-driven, editor-only, deprecated, or otherwise excluded from reliable save, duplication, copy, cook, or undo**;
- any **generated field with no matching authored payload field**, or a generated field whose flags or types disagree with what was authored.

The parent chain is bounded too: a Cue Type may inherit only from a native Cue base or from another Paper2D+ Cue Type that itself passes the whole contract.

The synchronous-behavior check is semantic, recursive, and cycle-safe. It follows collapsed/subgraphs, referenced macros, and specialized Cue Type parents, so hiding a Delay in a collapsed node or inheriting an invalid parent does not make it legal. The same policy runs for direct Blueprint compilation, placement readiness, protected Save/Save All preparation, durable validation, and cook. A rejection names the Cue Type, behavior event, offending node/call, and graph path; Paper2DPlus neither strips nor rewrites the authored node. This policy tightens the already-documented shared-placement rule and does not change the durable schema version or fingerprint of a valid Cue Type.

If an older Cue-derived Blueprint contains graphs the envelope refuses, Paper2DPlus opens it through an explicit legacy-recovery warning instead of deleting the authored graph.

The Cue Type schema is **version 2**. A Cue Type that implements no behavior describes itself exactly as version 1 did -- same canonical text, same fingerprint -- so every Cue Type authored before v8.0.0 stays loadable, stays placement-ready, and re-saves cleanly with no restaging pass. Only a type that actually carries behavior is stamped version 2.

Set a useful class color and, when appropriate, a default Cue Tag under `Paper2DPlus.Cue.*`. For a Cue State, enable `Emit Updates` only when the behavior or a receiver genuinely needs per-frame updates; Begin/End is usually cheaper and easier to reason about. Choose the correct default network policy, then either implement the work in the Cue Type's own behavior events or leave the Cue as pure payload and implement it once in the actor, component, subsystem, or other receiver that listens for the Cue class/tag.

Cue objects are instanced placement data. Duplicating a placement makes an independent object; changing one placement does not silently change another, and each placement's behavior reads its own payload values. Renaming or changing the schema runs a compatibility preflight before any compiled class or existing placement is reinstanced.

What a placement is *not* is per-character: it belongs to the animation, so every actor playing that animation shares the one object. That is the whole reason behavior has to stay stateless -- see **Behavior must be stateless** above.

## Receive Cues in Blueprint

Listening is still how a project reacts to a cue it did not author, and the behavior events change nothing about it except the order: the cue's behavior runs first, then every listener is notified, at every site.

The easiest typed path is **Listen for Frame Cue** under `Paper2DPlus | Frame Cues`.

1. Supply the actor's Character Profile Component.
2. Choose a literal Cue Class. The output Cue pin adopts that concrete type, so its custom fields are directly available.
3. Optionally filter by `Paper2DPlus.Cue.*` tag. Class and tag filters can use hierarchical or exact matching.
4. Bind the lifecycle outputs you need:
   - **Triggered** for Cues.
   - **Began**, **Updated**, and **Ended** for Cue States.
5. Read the Context for actor, Profile Component, exact Character Profile, live playback component, evaluation mode, flipbook, animation name, current/previous key frame, loop wrap, compressed traversal, catch-up, network context, and Cue State end reason.
6. Call **Cancel** when the listener's owning flow should stop early. The listener also unbinds when its source or world ends.

Alternatively, bind the Character Profile Component's `OnFrameCue` delegate and branch on Cue class/tag yourself. `GetActiveFrameCueRanges` and `GetActorActiveFrameCueRanges` expose the currently admitted Cue States for inspection.

### Cue State end reasons

An End is not always ordinary completion. Receivers should release state for every End reason, not only `Completed`.

| End reason | Meaning |
|---|---|
| `None` | This notification is not an End. |
| `Completed` | Non-looping playback naturally finished after final-frame work, or an external owner reported natural completion. |
| `AnimationChanged` | The Profile or flipbook changed before the range exited normally. |
| `Interrupted` | A new externally owned playback generation superseded the prior generation. |
| `SourceRemoved` | The bound render source or the authored/composed Cue source disappeared. |
| `LoopReset` | A loop wrapped to a frame no longer covered by this Cue State. A state still covering the wrapped frame remains active. |
| `PlaybackStopped` | Playback was explicitly reported or observed stopped, including the Frame Cues preview's Stop action. |
| `ComponentDestroyed` | The Character Profile Component is ending or being destroyed. |
| `CueMutation` | Editor authoring changed a live placement/source while it was previewing. |
| `EditorReset` | Preview seek/session changes, compile/host reset, tool deactivation, or tool close ended the preview session. |
| `Forced` | An explicit generic safety teardown requested an End without a more specific boundary. |

One traversal may cross an entire short Cue State. In that case the lifecycle can be compressed into the same transition and `bIsCompressed` is true; receivers must not assume a real-time delay between Begin and End.

A listener End is guaranteed for every Begin at every teardown boundary. The cue's own `On Cue End` behavior has one documented gap -- a range whose Cue Type asset was deleted mid-flight -- which is why releases belong on the receiver. See **Every Begin gets an End** above.

Terminal handling is generation-scoped and first-boundary-wins. Once one boundary clears the active set, a repeated stop/completion report, later natural-finish signal, flipbook change, or teardown does not emit a second End.

### Playback ownership and explicit terminal reports

`SetFrameCuePlaybackSource` is the supported way to bind the live Paper component. Rebinding atomically ends the outgoing generation as `SourceRemoved`, unbinds its callbacks, binds the replacement, and warms the new source; it never starts or stops playback.

For ordinary component-owned playback:

- `UPaper2DPlusFlipbookComponent` reports frame, start, direct-stop, and natural-finish boundaries through its event-driven path. On natural non-looping completion, final-frame Cue work runs before `Completed`.
- A stock `UPaperFlipbookComponent` uses the Profile Component's fallback observation. A cue-carrying Profile/Layer is sampled every frame; a cue-free one retains the 20 Hz idle watch. Direct `Stop()` is closed as `PlaybackStopped` on the next relevant observation, and natural finish is closed as `Completed`.

Unreal's `Stop()` is non-virtual and sends no stop notification. Therefore **Stop then Play entirely between two observations cannot be inferred** on either compatibility path. Code that owns that sequence must report the stop synchronously before restarting.

An external player such as PaperZD may keep the render component stopped while driving `SetPlaybackPosition`, so `IsPlaying` cannot identify its sessions. **No integration code is required for cues to fire**: observed frame movement on the bound source counts as playback, so after an observed stop the next frame advance automatically reopens a native dispatch generation and every anchor fires normally. What the automatic path cannot know is *why* a session ended — a manually driven session that simply stops moving holds its last generation open until the next boundary (an animation change, a rebind, or teardown) closes it with that boundary's End reason. An integration that owns the timeline and wants exact session boundaries and End reasons uses the generic, Paper2D-native generation seam:

1. Call `BeginExternalFrameCuePlayback(ExpectedSource)` once for every playback session, including a same-flipbook restart, and retain its positive generation.
2. Drive the component and evaluate frames normally.
3. After the definitive final frame, call `ReportFrameCuePlaybackCompleted(ExpectedSource, Generation)` for natural completion; after a manual/cancelled stop, call `ReportFrameCuePlaybackStopped(ExpectedSource, Generation)`.

Both report functions are idempotent, reject a stale source/generation pair, and finalize Cue lifecycle only. They do not call Stop, Play, change the flipbook, or take ownership of PaperZD/game playback. `GetFrameCuePlaybackGeneration` exposes the current token for integrations that need to inspect it, but the value returned by Begin is the safest token to retain.

## Network policy

With Paper2DPlus replication disabled or in standalone play, every policy dispatches locally. With replication enabled:

| Policy | Authority | Owning client | Other clients | Dedicated server | Use for |
|---|---:|---:|---:|---:|---|
| Local Always | Yes | Yes | Yes | Yes | Per-machine data/cosmetics that never mutate gameplay |
| Authority Only | Yes | No | No | Yes | Projectiles, tags, damage requests, or any gameplay mutation |
| Cosmetic Only | Yes | Yes | Yes | No | World-visible VFX, audio, shake, and overlays |
| Owner Only | Locally controlled listen-server actor only | Yes | No | No | Owner-specific UI/camera presentation |

Cues are suppressed during catch-up so late join or replicated playback correction does not replay one-shot side effects. An already-active cosmetic Cue State may be rebuilt with `bIsCatchUp=true`. Authority-only Cue State beginnings do not replay during catch-up.

The policy gates **both** halves of a notification -- the cue's own behavior and the listener broadcast -- at the same admission point. It does not replicate the side effect: behavior (or a receiver) spawning a replicated actor must still do so on authority using the project's normal networking rules.

A Cue Type created through either creation flow starts at **Cosmetic Only**. It is an ordinary class default, visible and editable in the restricted editor's Class Defaults under **Networking**, and carried across recompiles like any other. The runtime base default is Local Always, which would have run every authored footstep on a dedicated server with no designer action.

A Cue that mutates gameplay state should be set **Authority Only** by its author: making such a cue cosmetic would, for example, spawn projectiles on clients while skipping the server. Purely cosmetic cues stay Cosmetic Only, which is also the default a newly created Cue Type gets.

> **Changing a Cue Type's Net Policy class default is a breaking edit, not a tweak.** Net Policy is an ordinary reflected property, so it is delta-serialized: a placement whose value equals its class default is not written into the asset at all, and takes whatever the class default says on the next load. Changing that default therefore silently rewrites the behavior of every already-saved placement that accepted it -- a migration break with no diagnostic and no undo. If a default genuinely has to change, it needs a versioned `PostLoad` migration that reads the old default into an explicit stored value first -- never a bare edit to the class default. (Through v7.0.0 the six native built-ins' defaults were frozen by contract and pinned by test; those classes and their pins were deleted in v8.0.0, but the delta-serialization hazard they were protecting against applies unchanged to the Cue Types you author.)

## Preview in the editor

The **Frame Cues** tab previews a Cue the way the game dispatches it: your behavior events run through the same helper in an isolated `EditorPreview` world, then the host notifies any optional adapters. **Preview Selected**, scrubbing, and playback all use that path.

The viewport keeps Unreal's standard editor grid and the render flags needed for editor primitives, particles, translucent effects, and post-process output. Switching to Frame Cues also gives its playback controller keyboard focus, so **Space** starts playback on the first press without a preparatory click.

**Ordinary Cue behavior works without adapter registration.** The preview Context contains a transient owning actor, an inert Character Profile Component, the exact Character Profile, a live registered Paper flipbook component, the current flipbook/animation/frame snapshot, and the matching editor evaluation mode. During each synchronous behavior event, Cue `self` resolves that isolated world just like an Unreal or PaperZD notify resolves its preview component. Spawn Actor, Paper2D/particle effects, traces, debug drawing, and world-aware sound can therefore appear in the preview viewport without a second Blueprint or adapter. **Resolve Frame Cue Anchor** uses the same geometry and result contract as runtime, so equal Profile/component/frame inputs resolve byte-consistently.

**The preview is isolated, not a copy of the game map.** It does not supply the project's GameMode, GameInstance, network driver, AI/navigation world, or custom player-character class. Branch on `Is Editor Preview` when behavior depends on those game-specific services. Cue placements remain shared stateless objects: create state on the preview actor/world, not on the Cue, and do not use latent Cue graphs.

**Seek and teardown are deterministic.** A manual seek first pairs any active Cue State End with `EditorReset`, then replaces the isolated preview world before seeding the destination frame as `EditorScrubSeek`. **Preview Selected** uses `EditorSelectionPreview`; timeline play uses `EditorPlayback`; pressing Stop during that playback ends active states with `PlaybackStopped`. Animation changes use `AnimationChanged`, scope/source removal uses `SourceRemoved`, and Cue edits use `CueMutation`. Compile/host reset, Clear, deactivation, and closing the editor use `EditorReset`. A forced End retains the outgoing session's evaluation mode and last usable source frame. Playback does not rebuild per frame, and a Cue State spanning a loop remains continuous across that loop just as it does at runtime.

**Sound retains deterministic ownership.** `Play Cue Sound` works in both places. In game it plays at the owning actor's location and returns false on a dedicated server; in preview it routes to editor-owned preview audio so scrub-back, stop, tool switch, and closing the tab silence it. Ordinary world sound nodes can also use the isolated world, but `Play Cue Sound` remains the easiest teardown-safe choice.

**A failing behavior is contained, never quarantined.** The first failure of a given *placement* in an editor session logs one error naming the placement and its Cue Type, and badges that placement on the timeline. Preview keeps dispatching, the same behavior runs again on the next crossing, and a second broken placement gets its own report -- one broken placement must never silence a sibling. That per-placement granularity is deliberate. None of this exists in game: runtime behavior is not wrapped, not badged, and not suppressed.

Set `Skip in Editor Preview` on a placement when its data should stay authorable and runtime-visible but nothing should run or draw for it while scrubbing.

### Preview adapters versus behavior

**A Cue Type's behavior is the default preview implementation.** Paper2DPlus requires no preview registration — the one built-in Spawn Flipbook Cue and every designer-authored Cue Type preview the same way. Author the gameplay behavior once; the Frame Cues viewport supplies its preview world automatically.

The adapter framework remains an advanced, editor-only extension for output that is intentionally abstract rather than gameplay-world behavior—for example a camera-shake approximation, explanatory overlay, or project-specific visualization. Derive from `UPaper2DPlusFrameCuePreviewAdapter` and register it under **Project Settings > Paper2D+ Frame Cue Editor > Preview Adapters**. Matching adapters run after ordinary Cue behavior and never suppress it. (Through v7.0.0 the six built-ins shipped five bespoke native adapters; those adapters were deleted with their Cue classes in v8.0.0, while the compatibility framework remains.)

An adapter can still create host-owned:

- play preview audio;
- show effect and projectile proxies;
- draw overlays and shapes;
- approximate camera shake by offsetting the editor viewport camera.

Those adapter operations do not spawn gameplay actors, apply tags, run damage, or execute a real camera-shake graph. Ordinary Cue behavior owns the real preview-world path; adapters only add their controlled output beside it.

Creating/registering an adapter is a project configuration action and is not undone by Ctrl+Z. Runtime code and packaged builds do not depend on preview adapter classes.

## Cue variables and the Effect Library

Effect Profiles remain an independent tagged visual library. Use their editor and Blueprint query nodes when the project needs curated discovery, but a Cue Type does not acquire hidden library semantics from one of its variables. Designers author the Cue's variables and behavior directly.

For frame-zero warming, use a scalar soft Paper Flipbook variable. Before traversal, the Character Profile Component structurally discovers every such variable on each placement—resolved or unresolved—and retains the current animation's referenced art. The Frame Cues editor mirrors that warm set. No marker, declaration, Cue class, Catalog lookup, or Effect Profile lookup participates, so per-frame dispatch and Slate paint stay load-free. Changing animations releases the previous retained set for normal garbage collection when nothing else owns it.

Removing a Flipbook from an Effect Profile does not rewrite an existing Cue, and Cue dispatch never resolves a Catalog, Effect Profile, or effect row name. (The native Spawn Effect Cue's `ResolveSpawnSettings` and deprecated `Get Effect Flipbook` getter were deleted with that class in v8.0.0.)

See [Effect Profiles](effect-profile-guide.md) for library setup.

## Validation and migration

Validation reports null Cues, anchors outside the animation, invalid Cue State spans, and invalid policy data. Redirectors are followed through Registry metadata without loading their destination. The built-in Spawn Effect validation codes were removed in v8.0.0, and TASK-167 retired the remaining Cue Effect-field library/filter codes with the special field concept itself. Frame exclusion/restoration and frame reordering remap Cue anchors with the animation data; Cues removed with an excluded frame are stashed for restoration instead of silently discarded.

If validation reports empty or null placements, select that animation and open **Frame Cues**. A notice above the timeline reports the number of missing placements; no timeline bars are fabricated because an empty slot retains no trustworthy Cue Type, frame, track, or payload. Choose **Remove all (N)** to delete only those null slots in one undoable transaction. Valid Cues keep their object identity, relative order, timing, payload, and named-track membership, and the repair persists when the Profile is saved. The notice cannot tell whether a slot was never assigned or its Cue Type was deleted because both load as the same literal null value.

Legacy notes:

- **Nothing is converted any more.** v8.0.0 deleted the Frame Event -> Frame Cue conversion bridge and the legacy Effects/direct-data migrations along with the six built-in Cue classes they wrote into. Legacy executable Frame Event rows and legacy `Effects` rows remain loadable, hidden, and inert: never dispatched, never converted, and deliberately never erased from your assets.
- If you are upgrading from 6.x or earlier and have never opened your assets on 7.0.0, run that conversion on 7.0.0 first -- it does not exist in 8.0.0.
- Unsupported custom executable `UPaper2DPlusFrameEventBase` subclasses are retained as migration inventory but are not current authoring or runtime dispatch, and never will be -- the reversal above did not revive that hierarchy. Recreate the payload as a Cue Type, then put the work either in that Cue Type's own behavior events or in a receiver, before removing the legacy source.
- Old profile/name-based Spawn Effect data is retained as inert legacy data. Nothing in v8 resolves or bakes it; manually reproduce the intended payload in a Cue Type before clearing any source fields.
- The native Spawn Effect Cue's soft-storage upgrade path (deprecated hard compatibility property, effect-reference schema v1, `DirectDataVersion`) was deleted with the class in v8.0.0. Effect Profile rows themselves are unaffected and still upgrade normally.

Do not clear retained migration fields merely to hide a warning. First prove the replacement Cue and receiver preserve the intended payload and behavior.

## PaperZD boundary

Frame Cue timing is keyed to Paper2D flipbook key-frame indices and dispatched by the Character Profile Component. PaperZD may select animation sequences/flipbooks and drive the same live Paper component, but Frame Cues do not create, mirror, or execute PaperZD notifies. There is no required PaperZD type or AnimInstance field in the Context. Keep animation-graph state-machine behavior in PaperZD and character-profile per-frame payloads in Paper2DPlus.

When PaperZD or another external system owns a playback session, no integration code is required for cues to fire: the detection layer treats observed frame movement on the bound component as playback, even while `IsPlaying` reports stopped. An integration that wants exact session boundaries and End reasons can additionally bind the live `UPaperFlipbookComponent`, call `BeginExternalFrameCuePlayback` for each session, then report `Completed` or `PlaybackStopped` with the returned generation as described above. This gives Frame Cues exact End semantics without making PaperZD a runtime requirement.

## Useful Blueprint queries

The Blueprint library exposes read-only authored queries:

- `GetFrameCuesByClass`
- `GetFrameCuesByTag`
- `GetFrameCuesForFlipbook`
- `GetFrameCuesAtKeyFrameByFlipbook`
- `GetFrameCueRangesContainingKeyFrameByFlipbook`
- `GetActorActiveFrameCueRanges`

Null assets, invalid tags/classes, foreign flipbooks, and invalid key frames return empty results. Queries preserve authored order and do not execute Cue behavior.

## Make sure cues can be detected

Frame Cues are keyed to key-frame changes, so something has to notice those changes.

- **`UPaper2DPlusFlipbookComponent`** pushes flipbook and frame changes straight to the Character Profile Component. This is the recommended setup: it costs no tick at all and cannot miss a change.
- **A stock `UPaperFlipbookComponent`** has to be sampled instead. An actor whose Character Profile -- or whose equipped Character Layer -- carries any Frame Cue at all is now sampled **every frame**; an actor with no cues anywhere keeps the cheap 20 Hz idle watch that policy was originally designed for. The rate follows what the profile *can* dispatch, never what the current animation dispatches: an animation that starts and finishes between two samples is never observed at all, so its cues do not fire late -- they never fire.

Both paths also close an observed playing-to-stopped generation as `PlaybackStopped` and a natural non-looping finish as `Completed`. The final frame is evaluated before the natural terminal. A closed generation does not silence a source that keeps moving: frame movement observed after a terminal reopens a fresh native generation automatically, which is what keeps a PaperZD-style driver (stopped component, position written by hand) dispatching with zero integration code. The unavoidable sampling exception is a complete Stop-then-Play sequence between observations that lands back on the same key frame; a game/PaperZD owner that needs that exact boundary uses the explicit generation/report seam.

An animation that carries cues either dispatches, or says why it cannot. Two diagnostics fire once per actor, on screen in PIE and development builds plus a `LogPaper2DPlus` warning:

- **the poll is too coarse for this animation** -- names the actor, the flipbook component, the requested sampling interval, the animation, the profile, and the animation's wall-clock life (authored duration divided by the magnitude of the play rate, so an animation sped up 8x is judged on the 50 ms it actually lasts, and a paused component is never reported);
- **this animation carries placements dispatch cannot deliver** -- names the actor, the component, the animation, the profile, and how many placements are empty or have lost their Cue Type asset. Character Profile validation lists them by index.

Both are deliberately conservative, and the limits matter:

- The coarse-poll warning judges the **requested** poll interval, never one observed frame delta, so a routine hitch (a shader compile, a synchronous load) cannot latch a permanent, undismissable alarm on an actor that is already watching every frame it can.
- Consequently **sampling loss caused by a low frame rate is out of scope**: at 5 fps a 20 Hz watch can genuinely miss a cue and will not warn, because the interval it asked for was fine.
- The undispatchable warning latches once per **component**, so a second animation on the same actor with orphaned placements stays silent after the first report.
- Empty Layer placement slots are dropped during composition before that warning can see them, so at the Layer tier only a dead Cue Type class is ever reported. The base-profile tier is fully covered.

Silent misses that no diagnostic covers, by design: an animation shorter than one rendered frame, a same-frame `SetFlipbook` A -> B -> A, reverse playback (the traversal maths assume forward playback), and a poll coarser than a whole loop pass.

## Accessibility and troubleshooting

- The Cue timeline announces selected class, anchor, Cue State duration, and keyboard commands through accessible text.
- The Preview panel reports that behavior runs automatically and how many editor-owned or preview-world resources are active.
- If **Preview Selected** shows no visual, check `Skip in Editor Preview`, confirm the Cue Type implements the matching lifecycle event, and inspect the timeline's behavior-error badge and Output Log.
- If nothing fires at runtime, verify the actor has a Character Profile Component, the correct flipbook is playing through the resolved component, the listener is active, and the Cue's policy admits the current network context. Check the Output Log and the PIE viewport for the two detection diagnostics above before assuming the Cue itself is at fault.
- If a Cue Type is missing from **+ Add Cue**, look for it among the disabled rows at the end of the picker: the row carries the reason it cannot be placed, and clicking it opens the type for repair.
- If behavior works in PIE but not in preview, check whether it requires a game-specific actor class, GameMode/GameInstance, navigation, networking, or another service the isolated preview world deliberately does not create. Branch on `Is Editor Preview` or use an advanced adapter only for that project-specific abstraction.
- If a Cue State begins but state remains stuck, make sure the receiver handles every `Ended` reason and does not ignore forced teardown.
