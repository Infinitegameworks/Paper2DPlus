---
module: Paper2DPlusEditor
tags: [aseprite, import, reimport, directory-watcher, asset-registry, editor-services, ux]
problem_type: import-pipeline-and-live-edit
---

# Aseprite import & live-reimport patterns

Compounded from the TASK-182→186 epic (2026-08-19/20): the tag-selection import dialog, the
source-in-project workflow, and the live-edit watcher — including two rounds of REAL first-contact
use (the Body Type 01 set in BloodJunkies) that each falsified something the tests had passed.
Read this before touching `AsepriteImporter`, `AsepriteStructuralDiff`, `BulkSpriteExtractorWindow`, `AsepriteFactory`, or
`TextureWatcherService`.

## Part I — The headline failure: a service that never ran

**`FTextureWatcherService::Initialize()` was guarded by `if (GEditor)` inside `StartupModule()`.
GEditor is still null while plugin editor modules load, so the guard failed silently in EVERY
session — live .ase auto-reimport was dead code from the day TASK-71 shipped until 2026-08-20.**
The proof was one grep: a whole BloodJunkies session log with ZERO `TextureWatcherService` lines.

- **The rule:** never gate editor-service initialization on a bare `GEditor` check in
  `StartupModule`. Initialize on `FCoreDelegates::OnFEngineLoopInitComplete` (re-check GEditor
  inside so commandlets stay service-free), with a direct call for late-loaded modules.
- **The methodology lesson:** the suite was 768/768 green while the feature's WIRING was dead.
  Worldless seam tests prove logic, not liveness. A service needs at least one assertion that it
  actually STARTS (a boot log line to grep in gate evidence, an `IsInitialized()` probe) — and
  when a user says "nothing happens", grep the editor log for the service's name before touching
  its logic. Both first-contact reports of this epic were diagnosed from `BloodJunkies.log` in
  minutes: round one was a canceled dialog (no import had ever run), round two was the missing
  init line.

## Part II — Import dialog / factory patterns

1. **`UFactory::FactoryCreateFile`'s `InParent` is the PACKAGE being created, not the folder.**
   `InParent->GetPathName()` on a drop into `/Game/MainChar` is `/Game/MainChar/<FileName>` —
   using it as the output path nests every import inside a folder named after the file. Derive
   the folder with `FPackageName::GetLongPackagePath()`.
2. **A modal tool window must not be `IsTopmostWindow(true)`.** Pickers and buttons can open
   ENGINE dialogs (Content Browser "Save Asset As"), which then appear UNDER the topmost tool
   window — the user is typing into a dialog they cannot see. `AddModalWindow` already provides
   modality; topmost adds only the failure mode.
3. **"Protective" modes that silently skip output read as breakage.** Picking an existing
   Character Profile used to skip flipbook creation entirely (to protect curated data); the
   user's read was "the import made nothing". The shape that matches intent: the pipeline ALWAYS
   runs; what the picked target's CONTENT decides is only how results land — a created or EMPTY
   target is fully regenerated, a POPULATED one is populated ADDITIVELY (new entries append,
   same-name entries refresh art only, authored data untouched). The additive delivery is a pure
   seam (`AppendProfileEntriesFromImportResult`, idempotency-tested), and it is what makes the
   multi-file character workflow (five .ase files → one profile) fall out for free.
4. **Never copy loose source files INTO `Content/`.** Unreal's own content-directory monitor
   treats a new `.ase` under Content as an importable source file and prompts "new source file
   detected — import?" the moment your import finishes. Keep committed source art in a project
   folder OUTSIDE Content (`<Project>/SourceArt/`, flat by user preference) and register it as an
   external watch directory.
5. **Organize bulk output.** One 64-frame, 6-layer file materializes ~450 assets; flat in one
   folder they bury the Profile/Layer Profile they accompany. `Flipbooks/`, `Sheets/`,
   `Sprites/<prefix>/` subfolders (default on), primary assets at the root. Asset NAMES are
   unchanged so a same-settings re-import reuses the same packages.
6. **Create-or-select beats a modal.** The picker rows' "New…" buttons take a name and create the
   asset directly at the output path (or select an existing one with that name) — replacing the
   Save-Asset-As round trip that pattern 2 was hiding. UI terminology: the Character Layer asset
   is labeled **"Layer Profile"** in this dialog (user's term).

## Part III — Live-edit / watcher patterns

7. **Map watched files from ASSET REGISTRY TAGS, not loaded objects.** The original map only
   covered already-loaded layer assets — empty after every editor restart. Hidden tags
   (`Paper2DPlus.SourceAseFile`, `Paper2DPlus.SourceAseHash`) let the watcher map every SAVED
   asset without loading; the asset loads lazily (`TryLoad`) only when its file actually changes.
   Rebuild the map after `IAssetRegistry::OnFilesLoaded` — module startup precedes the initial
   scan.
8. **Content-hash stamps beat timestamps.** `ImportedAseContentHash` (MD5, stamped at import and
   every successful reimport) gives two behaviors timestamps cannot: an offline reconcile at
   startup (a git pull that landed while the editor was CLOSED produces no change event — compare
   hashes instead), and echo suppression (a change event whose content equals the stamp is the
   import's own write or a duplicate event — skip it instead of re-running the import the user
   just watched finish).
9. **Watch for `FCA_Added`, not just `FCA_Modified`.** Git materializes a pull as
   delete+recreate, so an updated source file arrives as an ADD; Modified-only filters silently
   miss pulled changes. (Aseprite's own safe-save can rename-over too.)
10. **Store source paths PROJECT-RELATIVE, resolve explicitly.** A path stored absolute is wrong
    on every other machine. And never resolve a stored relative path with bare
    `ConvertRelativePathToFull` — it anchors to the engine binaries directory, not the project.
    One make/resolve helper pair (`MakeStoredAsePath`/`ResolveStoredAsePath`), round-trip-tested.
11. **Full-fidelity reimport = replay the import.** One `.ase` fans out into sheet + per-layer
    sheets + sprites + flipbooks + profile entries + Layer Profile data; a diff-reimport of just
    one asset class leaves the rest stale (the user edits art and their FLIPBOOKS don't change).
    Stamp the import CONTEXT on the tracked asset (output path, prefix, organization flag,
    de-selected tag NAMES — names survive reordering, indices don't) and re-run the whole
    pipeline with `ExistingLayerAsset = self`: every creator is FindOrCreate-in-place, so sheet
    pixels, sprites, flipbooks, and the profile all refresh, and NEW tags become new flipbooks.
12. **Iterate a COPY when a downstream call rebuilds your container.** The full re-run's tail
    calls `RefreshAssetMapping()`, which empties and rebuilds the very map
    `ProcessPendingChanges` is iterating — a live pointer/iterator dangles mid-loop. Copy the
    entry array first; re-`Find` afterwards to write back.

## Part IV — Parser

13. **The moved-from-local read.** `OutData.AllFrameCels = MoveTemp(AllFrameCels);` followed by a
    lookup through the LOCAL `AllFrameCels` — every linked ("hold") cel silently composited
    blank, because the moved-from array is empty and the bounds check politely failed. When a
    function moves a local into its output, grep the remainder of the function for the local's
    name before shipping.
14. **Config-driven name conventions.** Artist layer names (`HitBox`, `HITBOX`) didn't match the
    compiled `attack*`/`hurtbox*` prefixes, so hitbox DATA layers composited into the visible
    art. Prefix→type rows live in `UPaper2DPlusSettings::HitboxLayerNamePrefixes`; an EMPTIED
    list falls back to compiled defaults (a config mistake must not silently bake data into art);
    the `socket_` convention stays fixed and is checked first so a configured prefix cannot
    shadow it.

## See also

- `ue-plugin-shipped-gameplay-tag-sources.md` — the module-loading-phase sibling trap (tag
  manager not constructible at PostConfigInit).
- Host repo `docs/solutions/ue-worldless-automation-test-patterns.md` — the crafted in-memory
  `.ase` buffer technique used by `Paper2DPlusAsepriteImportSelectionTest.cpp`.
- Host repo `docs/solutions/ue-gameplay-tag-color-registry-patterns.md` — the C4458 `Tag`-member
  shadow footgun (bit again in this epic: locals named `Tag` in SWidget-derived code).
## Part V — Batch intake and structural-diff reimport (TASK-189)

Compounded from rebuilding the `.ase` UX on the Bulk Sprite Extractor after the user's verdict on
the per-file modal: *"using the importer some more was awful — we're trying to build things that
already exist."*

16. **Order decides whether a rename is a rename.** Every creator in this pipeline is
    FindOrCreate-in-place, so a structural diff has to run and apply its renames BEFORE the pipeline
    writes anything. Rename first and the generated sheet/sprites/flipbook move to the new name, the
    creation pass refreshes those same packages, and existing references survive through the
    redirector. Run the diff afterwards — the obvious reading of "diff the result" — and the new
    assets already exist under the new name, the old ones are stranded, and the layer is duplicated
    on the asset. That forces TWO apply points, at the two places their inputs first exist: tags
    before the profile is populated (`AppendProfileEntriesFromImportResult` matches by NAME, so a
    renamed tag would mint a second entry beside the authored one and strand every combat/timing/cue
    edit on the dead name), layers after the normal-map pairing is known and before the per-layer
    materialisation.

17. **Read the old record before the code that overwrites it.** `UpsertAseSourceContext` returns a
    live reference the import immediately fills with the NEW hashes, and it can reallocate the
    context array. Any old-side read has to happen at or before the stamping site, and no pointer or
    reference into that array may be held across the import call. Copy the values out.

18. **One derivation of a generated asset's name and folder, shared by everyone who touches it.**
    The rename phase and the creation loop each need "where does this layer's sheet go" — and if
    they ever disagree, the rename moves an asset to a path the loop then does not find, and the
    layer silently duplicates. Factor the derivation into one helper and call it from both.

19. **Three name spaces meet on one tag, and they are not interchangeable.** The layer asset keys
    its animation mappings by the RAW tag name; the flipbook package is `SanitizeAssetName("<prefix>_<Tag>")`;
    the profile entry is that with the prefix stripped. A rename must be translated through each one
    — and the profile's own rename funnel deliberately excludes the layer-asset side (it is reachable
    only through a one-way soft pointer), so that half is re-keyed explicitly.

20. **Pixel identity is stamped at import, never read back.** Per-layer hashes cover that layer's OWN
    cel pixels, so edits elsewhere in the file never break a pairing. Per-tag hashes cover the
    composited canvas over the tag's frame range, so ANY layer edit perturbs every tag hash: tag
    rename auto-resolve fires reliably on a rename-only save (the common case, because the watcher
    fires per save) and degrades to delete-plus-add when a rename and a pixel edit land in the same
    save. That degrade is the safe default and is reported; do not "fix" it by loosening the equality
    rule.

21. **Unattended paths report loudly and never prompt.** The watcher drives the whole reimport, so a
    modal there is a hang. An editor notification with counts plus a walkable Message Log page naming
    every decision is the shape that works in both attended and unattended runs — and a reimport that
    changed nothing must leave the package undirtied, or every save round-trips a dirty asset.

22. **One predicate keeps a new row kind out of every phase that would choke on it.** Adding `.ase`
    rows to the bulk extractor's shared state array meant they flowed into the per-texture cell-size
    map, the auto-pad count, the texture commit loop and the trim delete-source prompt — each of which
    would resolve the row's NULL texture pointer and report it as a grid failure. `StatusExcludedFromExtract`
    already existed as the de-bake lever; teaching it the new statuses fixed all four sites with no
    edits at any of them. The gates that then still need narrowing are the ones asking a TEXTURE
    question of the whole batch (`AreAllGridsConfirmed`, the blocker text's cell-size steps) — an
    `.ase`-only batch otherwise reports "all grids confirmed" and dead-ends in Auto-Pad, or reports
    "every row is an accepted de-bake source" for a batch holding no textures at all.

23. **A Content Browser drop delivers ONE file, whatever the shape of your cancel.** `UAssetToolsImpl::ImportAssetsInternal`
    declares `bImportWasCancelled` once OUTSIDE its per-file loop and the loop condition reads it — verified
    identical in 5.0 through 5.8. So the `nullptr` + `bOutOperationCanceled = true` return that suppresses
    the engine's import-failed dialog also stops the remaining files, and there is no return shape that does
    one without the other. A factory that hands files to a window needs its own multi-file door (a
    multi-select picker, a window-side drop target) and should say so in its hand-off log line.

24. **Adopt the created target INSIDE the batch loop.** The batch's Layer Profile is adopted from
    whatever the first row created so every later row merges into it. Doing that after the loop
    reproduces exactly the failure the batch model exists to fix — every file minting its own
    `<prefix>_Layers`.

25. **Retiring a modal means moving its OWNERSHIP, not just its widgets.** The `.ase` import dialog was
    handed `FAsepriteParsedData` + per-layer buffers owned by the factory, which outlived it. A bulk row
    deliberately keeps only a parse SUMMARY, because a 20-file batch that pinned every decoded frame
    would cost hundreds of megabytes. So the rehomed editor (`SAseRowImportEditor`) had to take over
    ownership: parse on construction, release on destruction, full pixel cost for exactly as long as its
    window is open. Copying the widgets across without moving that decision would have reintroduced the
    residency the batch model exists to avoid — or shown an empty preview.

26. **A child widget holding raw pointers into its PARENT's members outlives them by default.**
    `ChildSlot` lives on `SCompoundWidget`, so it is destroyed AFTER the derived class's members: a
    preview canvas pointing at buffers the parent owns is left dangling during teardown. It only *looks*
    safe when the child's destructor happens not to dereference them. Detach `ChildSlot` (and reset any
    child handles) at the top of the derived destructor, so the ordering is guaranteed rather than lucky.
    The retired dialog never hit this — its data was owned by the caller.

27. **An identity written by the producer and not understood by the consumer is the same as no identity.**
    Export/Import Organization recorded `AssetPath` from `State->Texture`, which is null on a `.ase` row,
    and the import lookup keyed the same way — so `.ase` rows exported a blank key, and a row renamed in
    session could never be matched back. The fix is one shared funnel (`MakeRowIdentityKey`) called by
    both sides, not two symmetrical-looking expressions. Same family as the registry-tag rule in Part III:
    change producer and consumer together, or make it impossible to change one alone.

## Part VI — Incremental reimport gates (TASK-192, 2026-08-26)

A covered reimport now writes NOTHING (a byte-changed no-op dirties exactly one package — the
restamped Layer Profile); a one-layer pixel edit rewrites that layer's sheet, the composited
sheet, and only the sprites whose tight bounds provably moved; a pure retime rewrites one
flipbook. The gate seam is `FAsepriteIncrementalWrite` (`AsepriteIncrementalWrite.h/.cpp`);
every verdict lands in `FAsepriteImportCostReport::DecisionLines` and the non-modal **Aseprite
Import** Message Log audit page. `bEnableAseLiveReimport` (default on) gates only the watcher;
**Force Full Reimport** on the Layer Profile context menu replays everything gates-bypassed,
even with the setting off. The rules that keep the gates honest:

28. **Gate on stamps + registry existence, never on residency.** Generated assets are NOT in
    memory during a reimport — `FindOrCreateAssetInPackage` is a `FindObject`, and sheets,
    sprites, and flipbooks are soft references, so `FindObject` null means "not loaded", never
    "not there". The only object guaranteed loaded is the Layer Profile itself, so the stamps
    (`LayerContentHashes`, `CompositeContentHash`, `TagStructureHashes`,
    `NormalLayerContentHashes`, the stamped grid) live on ITS `FAsepriteSourceContext`, and
    existence questions go to the Asset Registry. Absent stamps FAIL CLOSED to write — an
    un-stamped asset population is indistinguishable from a pre-gate one. Corollary for direct
    `ImportAsLayeredAsset` callers: the gate context resolves from `Settings.ExistingLayerAsset`,
    so a reimport that omits it silently degrades to a full rewrite — the perf replica measured
    exactly that for one round before threading the leg-1 asset through.

29. **Payload rides the container's verdict; existence never does.** A sprite whose sheet
    skipped may skip its pixel work wholesale, but a registry-missing package always writes
    regardless of hashes — a user who deletes one generated sprite must get it back on the
    next save.

30. **Derive geometry from the buffer in hand; never call the engine's rebuild to measure.**
    `RebuildData()` is not a query — `RebuildCollisionData` allocates a `UBodySetup` every call.
    The gate replicates `FindTextureBoundingBox` semantics against the composited cell already
    in memory (strictly-greater `alpha > clamp(int(threshold*255),0,255)`, the engine's shrink
    order, the all-empty degenerate of 1×1 at bottom-right) and compares with the serialized
    `RenderGeometry`/`CollisionGeometry`, read via `FindFProperty` because both are protected.
    Geometry modes decide provability: `SourceBoundingBox`/`FullyCustom` never read pixels
    (pass), `TightBoundingBox` compares, `ShrinkWrapped`/`Diced` are unprovable from a bounding
    box (always write).

31. **A byte-changed source producing no asset change must still restamp.** Otherwise the
    startup hash-reconcile re-queues the file forever. The restamp is the ONE legitimate
    package dirty of a fully-covered reimport — pin `PackagesDirtied == 1` in tests; it is the
    observable difference between "incremental" and "didn't run".

32. **The Asset Registry sees resident RF_Standalone objects that were never advertised.**
    Default registry queries enumerate live objects, so "this /Temp import did not advertise"
    cannot be asserted by querying the registry — listen to `IAssetRegistry::OnAssetAdded`
    broadcasts during the window, with a positive control proving the listener fires at all.

33. **Instrument before narrowing.** The plan attributed the ~3.5 s reimport stall to texture
    rebuild/DDC; the phase timers showed DDC encode at ~0.05 s (`TC_EditorIcon` is uncompressed)
    and the per-sprite `InitializeSprite` pass at ~3.3 s — each rebuild scans a whole-sheet
    `FAlphaBitmap` (~8.6 ms × 448 sprites). The bounds gate that skips the rebuild was therefore
    the time win; dirtied-package narrowing buys LFS churn, not seconds. The opt-in
    `Paper2DPlusPerf.AsepriteIncremental` lane replays the measured six-layer shape
    fresh-vs-covered on any machine.

34. **Preserve designer-tuned values by writing them only at birth.** The reconcile's
    full-rebuild path sets `PixelsPerUnit` only when it CREATES a sprite; a rewrite of a
    surviving sprite preserves the current value. Any "regenerate" that reapplies import
    defaults to surviving assets silently reverts designer tuning.

