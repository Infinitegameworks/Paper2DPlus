# Releasing Paper2DPlus

The version number is a promise to people who have already shipped against this plugin. This file is
the rule for what that number means and the checklist for changing it.

## Version scheme

`MAJOR.MINOR.PATCH` (semver). Two fields in `Paper2DPlus.uplugin` carry it and they move together:

| Field | Meaning |
|-------|---------|
| `VersionName` | The human version — `"7.0.0"`. This is what designers, Fab, and issue reports quote. |
| `Version` | A monotonic integer, `+1` per release, never reused and never reordered. Unreal uses it to compare builds. |

### What forces a MAJOR

Any one of these, on its own, is a major bump. All of them are breaks a consumer cannot discover
without their project failing:

- A Blueprint node is deleted, renamed, or changes its pin signature (including a pin's type — an
  `int32` → `float` return retypes every downstream wire).
- A `UPROPERTY`/`UFUNCTION` exposed to Blueprint is removed or renamed without a working redirect.
- A public C++ symbol is renamed or removed.
- An asset schema changes such that loading runs a migration, or saved data is dropped.
- A shipped feature is removed.

Redirects reduce the pain but do not downgrade the bump: `Config/DefaultPaper2DPlus.ini` keeps old
`.uasset`s loading, and C++ call sites still have to change.

### What is a MINOR

Additive only, and existing content keeps working untouched: new Blueprint nodes, new asset types,
new editor tools, new optional fields with safe defaults.

### What is a PATCH

Bug fixes and internal refactors with no API, schema, or authored-data change.

## The CHANGELOG is the source of truth for what a version contains

`CHANGELOG.md` has exactly one `## Unreleased` section at the top while work is in flight. Its
heading names the version being built toward and the bump level, so nobody has to re-derive it:

```
## Unreleased — targeting v7.1.0 (MINOR)
```

Every change that meets a MAJOR trigger above must say **BREAKING** in its entry and state what a
consumer has to do. If an entry is BREAKING, the heading's target version is wrong unless it is a
major — fix the heading when you add the entry, not at release time.

## Release checklist

Do all of these in **one commit**. Splitting them is how versions drift.

1. Confirm the target version in the `## Unreleased` heading still matches the entries underneath it
   (a BREAKING entry added late means the target must become a major).
2. Rename `## Unreleased — targeting vX.Y.Z (LEVEL)` to `## vX.Y.Z — YYYY-MM-DD`.
3. Add a fresh empty `## Unreleased` section above it for the next cycle.
4. Set `VersionName` to `X.Y.Z` and increment `Version` by exactly 1 in `Paper2DPlus.uplugin`.
5. Verify `EngineVersion` — see the note below.
6. Run the release gates: the cross-version BuildPlugin harness, the headless `Paper2DPlus`
   automation suite (queue completion + zero failures), and `scripts/validate-paper2dplus.ps1`.
   A version is not cut until these are green; record the discovered test count in the evidence.
7. Tag the commit `paper2dplus-vX.Y.Z`.

### `EngineVersion`

`EngineVersion` in the `.uplugin` pins one engine. It is correct for a Fab upload, where each engine
version is a separate build, and misleading in the source repository, where the plugin supports the
whole 5.0–5.8 range. Set it per Fab build; do not treat the value committed here as the supported
range. The supported range lives in `README.md` and `ONBOARDING.md`.

## Why this file exists

`v6.1` was bumped in the `.uplugin` and never got a `CHANGELOG` section — so the record of what
shipped in it is gone. Then `v6.2` sat unchanged from 2026-04-29 to 2026-07-25 while roughly nine
breaking changes accumulated under `Unreleased`, which meant the published version number claimed
compatibility the code no longer had. Both failures are the same failure: the version and the
changelog moved independently. Step 2 and step 4 above are one commit for that reason.
