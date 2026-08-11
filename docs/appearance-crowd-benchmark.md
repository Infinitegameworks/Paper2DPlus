# Runtime Appearance Crowd Benchmark

The U31 gate measures Paper2DPlus's hybrid renderer against the exact all-live compatibility path in a real rendering session. It is a platform calibration tool, not a synthetic primitive-count claim.

Run it from the repository root with Unreal Editor closed:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/profile-paper2dplus-appearance.ps1
```

The script deliberately does not pass `-nullrhi`. It runs only `Paper2DPlus.AppearanceCrowd.Render.Profile200`, checks the automation result and every hard gate, and leaves two artifacts:

- `Saved/Automation/U31-AppearanceProfile-<timestamp>/` — Unreal's automation report and execution log.
- `Saved/Automation/Paper2DPlusAppearanceCrowd/render-profile-report.json` — the stable schema-v3 comparison report consumed by humans and CI.

### UE 5.0–5.8 real-RHI matrix

For the release proof, run the same profile sequentially against the existing `P2DPHost` with every
supported engine:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/run-paper2dplus-profile200-matrix.ps1 `
  -Versions '5.0','5.1','5.2','5.3','5.4','5.5','5.6','5.7','5.8'
```

The matrix copies the current plugin source into `C:\Users\bluey\p2dp_hosttest`, removes only guarded
build/plugin descendants of that explicit host root, rebuilds `P2DPHostEditor` for each engine, and
runs one real-D3D11 editor at a time under a watchdog. Do not run it in parallel with another harness
that uses the same host.

Each version must finish the `Automation Test Queue Empty` sentinel with exactly one test, emit a fresh
automation index, report schema 3 with a non-null RHI and `TimingStatus = RenderCapableProductionPathMeasured`,
and pass all eight gates:

- `PrimitiveCapsPassed`
- `WarmGameThreadImproved`
- `WarmRenderThreadImproved`
- `CacheBudgetPassed`
- `CompositeWorkMillisecondsBudgetPassed`
- `SynchronousLoadGatePassed`
- `NetworkAuthorityAndPayloadContractPassed`
- `ProductionComponentSubsystemCachePathPassed`

Evidence is preserved beneath
`C:\Users\bluey\p2dp_xver\reports\profile200\<run-id>\UE-<version>\`; `summary.json` and `summary.md`
are rewritten after each version so a later failure cannot erase earlier metrics.

The normal null-RHI suite still runs `Paper2DPlus.AppearanceCrowd.Reference200.PrimitiveAndWorkGates`. That deterministic test writes `reference-policy-report.json`; it never fills unavailable timing fields with invented zeroes and points to the real-render command above.

## What is measured

The harness creates one transient Game world with 200 character actors and no saved package or project content. For every 4-, 8-, and 12-layer case it runs shared, 20-key repeated, and all-unique appearance distributions.

The animation source is intentionally identity-real and image-equivalent. It contains twelve resident, pointer-distinct `UPaperSprite` objects: frame zero is initialized once and frames 1–11 are duplicated UObjects with the same render bytes. The all-live baseline therefore performs real per-frame `SetSprite` swaps instead of repeatedly assigning one pointer, while both renderer paths still draw equivalent art. The A/B appearance descriptors are also identity-distinct but render-equivalent so cache-key transitions are measured without making one side visually more expensive.

Cold activation and warm steady-state timing are deliberately separate. Cold descriptor/outfit mutation, recipe creation, cache admission, composite construction/submission, pending reuse, and queue drain belong to the cold/cache/work measurements. Before a timed warm pass, the harness establishes the real 140 Far / 50 Near / 10 Changing production layout and resolves its required resources. The warm clock then keeps those committed appearance selections stable while advancing the same frame updates and scene captures for all 200 real components in both all-live and hybrid fixtures. It does not repeatedly call outfit-authoring paths or rebuild recipes inside the warm comparison. This keeps `WarmGameThreadImproved` and `WarmRenderThreadImproved` focused on steady presentation policy/update cost while the separate cold gates continue to police activation expense.

Each fixture compares:

- all-live: every actor registers its authored layer count;
- hybrid: 140 far actors register one primitive, 50 near actors register one base plus two independent channels, and 10 changing actors register the authored count;
- warmed game-thread appearance update and policy time after the 140/50/10 layout is established, with all 200 real components advanced and no repeated outfit reauthoring;
- render-thread work bracketed around those same stable-descriptor updates and offscreen scene captures;
- GPU absolute timestamps when the current RHI exposes them, otherwise an explicit unavailable flag;
- cold cache misses/shared-pending reuse, warm prepared hits, 12-frame build-queue depth, drain frames at the configured hard unit cap, deterministic cache reservations, and initialized render-target resource bytes reported by the active RHI;
- the exact replicated-property **value payload** for one `FPaper2DPlusRepWardrobeState` change and 200 independent changes. The test builds the struct's `FRepLayout`, calls `SerializePropertiesForStruct` through `FNetBitWriter`, records payload bits plus ceiling bytes, round-trips the payload, verifies `UPaper2DPlusLayerRenderComponent::RepWardrobe` is the reflected replicated property, and recursively rejects tier/cache/pixel/texture/object state;
- an authority probe with 200 logical actors and simulated 0/1/4/32-client fan-out. Authority publication remains exactly one snapshot per logical change rather than multiplying in the setter path, a late join applies the latest snapshot, and the dedicated-server fixture allocates no visual children.

`ProductionComponentSubsystemCachePathPassed` is the anti-simulation gate. The render-capable half constructs 200 real actors with registered `UPaper2DPlusFlipbookComponent`, `UPaper2DPlusCharacterProfileComponent`, and `UPaper2DPlusLayerRenderComponent` instances, proves each appearance component reaches BeginPlay, and drives the real `UPaper2DPlusAppearanceBudgetSubsystem`, render backend, and process-local composite cache. It checks the expected 140 Far / 50 Near / 10 Changing tier split, granted live/build caps, exact primitive total, expected resident composite-key count, initialized RHI resource bytes, queue drain, and load-free steady state. Automation overrides only the deterministic view/config inputs and evaluation timing; it does not replace component resolution, scheduling, cache admission/build, GPU resource creation, or handoff with a parallel model.

The schema-v3 network measurement is intentionally narrower than wire traffic. It excludes RepLayout handles/change masks, actor-channel/bunch/packet headers, packet handlers, reliability, relevancy, retransmission, and per-connection fan-out, so `PayloadBytesPerChangeCeil` and `PayloadBytesFor200IndependentChangesCeil` are not packet-capture or on-wire byte counts. `SerializedDescriptorBytesPerChangeEstimate` and its 200-change sibling remain only as explicitly labeled compatibility aliases for the former persistent-archive estimate; no gate consumes them. Measure transport with Unreal's network profiler or packet capture under the project's real topology.

The stable report records the engine, platform, RHI, CPU, project budgets, measurement method, every raw fixture, improvement percentages, network-authority probe, and all eight gate results listed above. Game- and render-thread improvement are required for every fixture. GPU time is always recorded when supported, but is informational because timestamp support and scene cost differ by RHI.

## Shipped reference budgets

The default profile is intentionally conservative and remains opt-in:

| Setting | Reference value |
|---|---:|
| Maximum Concurrent Live Handoffs | 10 |
| Composite Build Units Per Frame | 2 |
| Composite Work Budget | 0.300 ms of game-thread preparation/submission per frame |
| Maximum Independent Channels | 2 |
| Transient Composite Cache Bytes | 128 MiB |
| Near Tier Distance | 3,000 cm |

The 0.300 ms work budget is measured rather than speculative. The 2026-07-10 UE 5.8/D3D11 reference run on an AMD Ryzen 7 5800X measured a 12-layer maximum of `0.0483989716 ms` per submitted unit, or `0.0967979431 ms` for the independent two-unit cap. The strictest production-shaped sample in the same nine-fixture run was the first-use 4-layer submission at `0.1362003386 ms` per unit. Calibrating against that stricter observation gives `0.1362003386 × 2 × 1.10 = 0.2996407449 ms`; rounding upward to 0.300 ms preserves 10% measured headroom for two units. Relative to the requested 12-layer pair, the same cap retains 209.9% headroom. `Paper2DPlus.AppearanceCrowd.Reference200.PrimitiveAndWorkGates` asserts this checked-in rationale, while every real-render rerun must still prove that all nine current measurements fit. This remains the checked-in UE 5.8 calibration rather than a claim that one value is optimal on every hardware class. The final-candidate UE 5.0–5.8 D3D11 matrix subsequently passed 9/9 with schema 3, all eight gates true, and zero reasons at `C:/Users/bluey/p2dp_xver/reports/profile200/profile200-337686-all-20260712-011822Z/summary.json` for source fingerprint `3376861DB80B9D99D4851D93D3D133D0EACFD2ACD18B7D14D6CE617F5F9EDDB2`.

The retained numeric UE 5.8/D3D11 calibration run is preserved at `Saved/Automation/FinalU31-AppearanceProfile-SourceFrozen-20260710-212052/index.json` with the stable schema-v3 report at `Saved/Automation/Paper2DPlusAppearanceCrowd/render-profile-report.json`. It passed all eight gates with the exact 140 Far / 50 Near / 10 Changing split and 330 / 370 / 410 primitives for 4 / 8 / 12 layers. Across the nine fixtures, hybrid warm game-thread time was 8.09–11.56 ms versus 31.63–109.63 ms all-live, and hybrid render-thread time was 32.72–108.12 ms versus 174.69–739.73 ms all-live. The strictest current submission was `0.1283995807 ms` per unit, or `0.2567991614 ms` for two units, leaving 16.82% measured headroom under 0.300 ms. The historical calibration above remains the stricter checked-in rationale; one OS-preemption outlier is not used to loosen a hard budget when immediate same-binary reruns remain below it.

The cache-byte, build-unit, and measured-milliseconds limits are independent hard caps. A build already in progress may consume the remaining milliseconds, but no later unit starts after either work cap is reached. Re-run the benchmark on each target hardware class before changing them. The millisecond budget covers game-thread recipe preparation, target creation, and GPU submission; render-thread/GPU timings remain measured diagnostics. A faster GPU does not justify a larger live-handoff cap if game/render-thread gates regress; a larger cache is useful only when the report shows eviction or cold-queue pressure for the project's real sprite dimensions and animation lengths.

The test fixture uses twelve 64×64 composite frames per appearance key for stable cross-machine cache accounting. `CacheReservationBytes` is the deterministic admission charge; `InitializedRenderTargetResourceBytes` is the engine-reported initialized resource size and is available only in a render-capable run. A project's real art may be larger, so use the runtime overlay's reserved bytes, queue, build time, hits/misses/evictions, synchronous-load violations, and descriptor counter alongside this report. Treat the report's `Network` object as exact replicated-property value-payload evidence with an explicit non-wire boundary, then measure real network traffic separately with the project's multiplayer topology and packet tooling.
