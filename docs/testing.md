# Testing and Release Verification

Paper2D Plus has no hosted CI. Verification runs on the maintainer's build machines against nine
locally installed engine versions (UE 5.0 – 5.8), and the results of every release's gate ladder are
recorded per release. This page describes what the test suite covers, how to run it against your own
engine install, and exactly what a release must pass before it ships to Fab — including the last
fully recorded run.

The gate-ladder numbers recorded in this file are transcribed from the release run's `summary.json` (written by
`scripts/run-paper2dplus-release-gates.ps1` in the LostRadiance host project), never estimated by hand.

## The automation suite

The plugin ships its tests in-tree, under `Source/Paper2DPlus/Private/Tests/` and
`Source/Paper2DPlusEditor/Private/Tests/`. As of late August 2026 the suite is 900+ automation
tests, in three broad tiers:

- **Worldless unit tests** — the majority. Pure runtime logic exercised without a `UWorld`:
  frame-cue dispatch and lifecycle ordering, layer composition and appearance resolution, combo-chain
  derivation, combat scoring, hitbox/pivot math, migration paths, and validation rules.
- **Editor tests** — Slate panels constructed headlessly, editor models, transaction/undo behavior,
  import/reimport pipelines against real files, and asset-authoring invariants.
- **Cooked and packaged runtime proofs** — a separate lane that cooks and packages real content:
  negative tests that must *fail* cooking (an invalid cue envelope refusing to cook is part of the
  product's safety contract), and positive proofs that cue dispatch works in a packaged client with
  no editor binary present.

The suite follows a strict test policy: behavioral tests must assert caller-visible behavior, a
missing prerequisite is a failure rather than a silent skip, and every new behavioral test is proven
able to fail — a deliberate production mutation must turn it red for the intended reason before it
counts. Suite size is not treated as a quality metric; tautological tests get deleted.

## Running the suite yourself

The ordinary lane runs headlessly on any engine install that has the plugin in a C++ project. From a
command prompt, with placeholder paths substituted for your own:

```
"<UE install>\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "<path to YourProject.uproject>" ^
  -ExecCmds="Automation RunTests Paper2DPlus; Quit" ^
  -TestExit="Automation Test Queue Empty" ^
  -nullrhi -unattended -nop4 -NoSplash ^
  -ReportExportPath="<output folder>"
```

Then read `index.json` in the report folder — judge results from the report, not from the process
exit code alone. The `Paper2DPlus` filter selects the ordinary namespace; a small number of
renderer-dependent tests live in separate lanes (`Paper2DPlusRender`, `Paper2DPlusProfile200`) that
require a real RHI, so drop `-nullrhi` and substitute those filters to run them. Expect discovered
test counts to vary slightly by engine version — a few tests are version-conditional.

## The release gate ladder

Before every Fab release, the maintainer runs a fixed ladder of gates on the release candidate. The
ladder is scripted and unattended; a skipped or unreached gate is recorded as not run, never as
passed, and the run stops at the first failure. Results are recorded per release.

- **Gate 1 — Warnings-as-errors compile.** The UE 5.8 editor and game targets are compiled fresh
  with `-WarningsAsErrors`. A release candidate that emits a single compiler warning does not
  proceed.
- **Gate 2 — Nine-engine packaging.** The plugin is packaged with the engine's standard
  `BuildPlugin` pipeline against every supported engine, UE 5.0 through 5.8 — nine builds. Each
  package is checked for build success, a verified engine-version stamp, and correct release
  content.
- **Gate 3 — Nine-engine host-runtime matrix.** The full automation suite is executed on every one
  of the nine engines against a frozen copy of the release source (fingerprinted, so any drift
  during the run fails closed). UE 5.0 and 5.8 run with a real renderer and add the render lane; the
  intermediate versions run headless and add the headless lane. On the two endpoint engines the
  matrix also runs the cooked-runtime proofs: two negative cook tests that must fail cooking (an
  invalid replicated cue payload, and deferred/latent cue behavior — both refusals are part of the
  shipped safety contract), and a positive proof that cue Trigger / Begin / Update / End dispatch
  works in a packaged client containing no `Paper2DPlusEditor` binary. Acceptance requires queue
  completion, exact result accounting, zero failures, zero not-run rows, and no silent-skip markers.
- **Gate 4a — Scripted visual tour.** A watchdog-supervised editor session opens every major
  authoring surface — the Character Profile tools, the Layer workspace, and the Effect, Combat, and
  Catalog editors — captures a screenshot of each, and asserts on-screen semantics: required
  controls present, retired surfaces absent, plus narrow-width, wide-width, and empty-first-run
  pressure views. The current tour is 32 screens, and all 32 must capture and pass their semantic
  checks.

## Last full-green ladder: v8.0.0 (2026-08-10)

The most recent release, **v8.0.0**, shipped with the complete ladder green:

- **Gate 1:** UE 5.8 editor + game Development builds, `-WarningsAsErrors`, clean.
- **Gate 2:** 9 of 9 engines packaged (UE 5.0 – 5.8), each with release-content verification.
- **Gate 3:** all nine engines green with zero failures — 758 discovered tests on UE 5.0, 760 on
  UE 5.1 – 5.7, and 761 on UE 5.8 (the spread is version-conditional tests). Both endpoint engines
  passed the full cooked-runtime proof set: both negative cook refusals held, and the packaged
  client dispatched all four cue lifecycle phases with no editor binary present.
- **Gate 4a:** the visual tour passed 32 of 32 screens, run twice.

Reaching that green surfaced and fixed six real cross-version defects along the way — among them a
per-module command-registration crash on older engines, a GC-related cue-dispatch assert, and an
orthographic post-processing clamp on UE 5.0 – 5.3. They are documented in the
[CHANGELOG](../CHANGELOG.md) under v8.0.0; none shipped in a released version.

## Since v8.0.0

The suite has kept growing on the current development head: 935+ tests green as of late August 2026,
covering the incremental Aseprite reimport pipeline and the sectioned layer-structure workspace
added since the release, with profile-owned directional animation sets complete on a branch pending
merge. Going forward, each release's gate-ladder
results — engine-by-engine test counts, packaging results, cooked proofs, and tour outcome — are
recorded in this file's per-release ladder sections, with a one-line summary in the
[CHANGELOG](../CHANGELOG.md) entry for that version.

## v9.0.0 release ladder (2026-09-04)

| Engine | Suite | Result | Notes |
|--------|-------|--------|-------|
| 5.0 | pending | pending | pending |
| 5.1 | pending | pending | pending |
| 5.2 | pending | pending | pending |
| 5.3 | pending | pending | pending |
| 5.4 | pending | pending | pending |
| 5.5 | pending | pending | pending |
| 5.6 | pending | pending | pending |
| 5.7 | pending | pending | pending |
| 5.8 | pending | pending | pending |

<!-- LADDER-NUMBERS: filled by the release run -->

### Known gaps

<!-- filled by the release run -->
