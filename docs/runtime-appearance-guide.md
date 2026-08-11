# Runtime Appearance

Runtime Appearance is the opt-in path for characters whose selected Layers change during play. It keeps one compact committed descriptor as gameplay/save/network truth, then lets each client choose exact all-live or bounded hybrid presentation.

## Authoring prerequisites

1. Set the Character Layer Asset to **Runtime Customizable**.
2. Give every Layer a stable `LayerId`.
3. Define complete Appearance Presets and one valid Default Appearance.
4. Add optional Exclusive Groups only where selections are mutually exclusive.
5. Assign the Layer Asset to `UPaper2DPlusLayerRenderComponent`.

The component initializes from the Default Appearance. Fixed / Baked assets reject runtime selection mutation because their Profile and Flipbooks already contain the compiled result.

## Blueprint and C++ operations

- `ApplyAppearancePreset` replaces the complete active Layer selection with a preset ID.
- `SetLayerActive` activates or deactivates one Layer. Activation automatically clears an active peer in the same optional Exclusive Group.
- `GetActiveLayerIds` returns the normalized committed IDs in global asset order.
- `GetAppearanceDescriptor` returns the serializable committed snapshot.
- `ApplyAppearanceDescriptor` restores a compatible snapshot after validating delivery identity, Layer IDs, and exclusivity.
- `SetLayerRecolorById` and `ClearLayerRecolorById` change transient local presentation only.

Preset application uses replacement semantics: a Layer omitted from the new preset becomes inactive. Presets never store order; the asset's `Layers` array controls rendering and gameplay precedence.

## Commit order

Every authoritative selection change follows one pipeline:

1. validate and normalize Layer IDs;
2. enforce optional Exclusive Groups;
3. compose Layer-owned gameplay in global order;
4. notify visual presentation;
5. publish the sequence-numbered descriptor when replication is enabled.

Preview, recolor, distance tier, cache state, primitive choice, and pending-build state never enter gameplay or the committed descriptor.

## Animation eligibility

Selection is global, but contribution is animation-aware. A selected Layer renders or contributes gameplay only when it owns the relevant authored animation row. Changing animation does not mutate the descriptor; it changes the projection of that descriptor for the current animation.

## Save and replication

`FPaper2DPlusAppearanceDescriptor` stores the current payload version, delivery identity, and normalized active Layer IDs. It contains no actor, component, pixel, texture, primitive, cache, tier, preview, visibility, or recolor state.

Replication is opt-in and server-authoritative. Paper2DPlus provides no client-to-server appearance RPC. Forward player intent through the game's authority channel, then call the committed mutation on the server. The replicated envelope carries a monotonic sequence and the current descriptor; latest sequence wins and duplicate delivery is idempotent.

SaveGame code should store the descriptor and restore it through `ApplyAppearanceDescriptor`, not reproduce selection rules independently.

## Presentation modes

Runtime Customizable always has an exact all-live fallback. Hybrid presentation is a separate default-off component setting:

- **Changing** characters use live Layer primitives.
- **Near** characters can retain independent live channels where required.
- **Far** characters can use a cached whole-appearance composite.

The world budget subsystem coordinates tiers, admission, work, and cache reservations. Dedicated servers allocate no visuals. Ordinary custom materials fail closed unless they satisfy the explicit verified recolor contract.

All-live and hybrid presentation consume the same committed descriptor, current animation, global Layer order, and Layer-authored placement. Presentation can change without republishing gameplay identity.

## Diagnostics and profiling

Use the debug overlay and appearance budget stats to inspect committed IDs, tier, primitive count, queue depth, reserved cache bytes, hits, misses, evictions, and measured composite work. Descriptor byte counters measure the reflected property value, not complete packet or on-wire cost.

Run the production profile with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/profile-paper2dplus-appearance.ps1
```

The schema-3 report must pass all eight correctness and performance gates. See [Runtime Appearance Crowd Benchmark](appearance-crowd-benchmark.md) for calibration and interpretation.

## Common mistakes

- Treating a preset as a partial patch. Presets always replace the complete selection.
- Storing display names instead of stable Layer IDs.
- Reimplementing exclusivity in UI or gameplay code instead of using the shared resolver.
- Calling committed mutations only on a client in multiplayer.
- Expecting a selected Layer to contribute on an animation it does not author.
- Enabling Fixed / Baked and runtime composition together.
- Putting preview, recolor, tier, or cache state into SaveGame or replication.
