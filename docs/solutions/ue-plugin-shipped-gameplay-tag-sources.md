# Shipping gameplay-tag ini sources inside a plugin (UE 5.0–5.8)

Paper2DPlus ships its recommended tag taxonomy as `Config/Tags/Paper2DPlusTags.ini`, registered from
`FPaper2DPlusModule::StartupModule` so every consuming project sees the tree with no per-project
config copies. Two traps cost a full debugging session; both are pinned by
`Paper2DPlus.AnimationTags.IniTaxonomyRegistration`.

## Trap 1 — registration timing from a PostConfigInit module

`UGameplayTagsManager::Get()` constructs a UObject. From a module loading at `PostConfigInit`:

- **Direct call in StartupModule** → fatal `Object is not packaged: None None` (UObject system not up).
- **`FCoreDelegates::OnInit`** → identical fatal on 5.8 (broadcast happens pre-UObject-init there).
- **`FDelayedAutoRegisterHelper(ObjectSystemReady)`** → no crash, but on 5.8 class *reflection* has not
  been processed yet (`StartProcessingNewlyLoadedObjects` logs after it), so
  `UGameplayTagsList::LoadConfig` silently reads **zero rows** and the search path is permanently
  marked loaded. On 5.0 the same phase fires *after* reflection — version-dependent semantics, the
  worst kind of trap.
- **`FCoreDelegates::OnAllModuleLoadingPhasesComplete`** — the correct hook, verified present on all
  nine engines 5.0–5.8: fires once every module loading phase finishes (reflection + config ready)
  and before `FEngineLoop::Init` loads any map/content in editor, cooked-game, and commandlet flows.
  `AddTagIniSearchPath` is idempotent and handles an already-built tree, so late/double registration
  is harmless.

## Trap 2 — tag-source ini files use PLAIN keys, not `+` array syntax

`+GameplayTagList=(...)` is **hierarchical project-config syntax** (`DefaultGameplayTags.ini`). Loose
tag-source files loaded via `AddTagIniSearchPath` → `LoadConfig` on a single file must use plain
repeated keys, exactly like the engine's own `Templates/TP_InCamVFXBP/Config/Tags/VPRoles.ini`:

```ini
[/Script/GameplayTags.GameplayTagsList]
GameplayTagList=(Tag="X.Y",DevComment="...")
GameplayTagList=(Tag="X.Z",DevComment="...")
```

With `+` prefixes the file parses to zero rows with no warning (source registers, tags never appear —
diagnose with `FindTagSource(...)->SourceTagList->GameplayTagList.Num()`).

## Test-design note

Pin registration with tags that exist **only** in the shipped ini. Tags duplicated in a host
project's `DefaultGameplayTags.ini` resolve from the project config and mask a dead plugin source.

## Packaging

`FilterPlugin.ini` already stages `/Config/...`; native tags (dimension roots) stay in C++
(`Paper2DPlusAnimationTags.h`) and are not re-declared in the ini.
