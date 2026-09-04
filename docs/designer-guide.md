# Paper2DPlus Designer Guide

This is the starting point for designers using Paper2DPlus. It describes where information belongs, the shortest setup order, and what must be handed to Blueprint gameplay. It reflects the current behavior-carrying Frame Cue, visual Effect Profile, advisory Combat Profile, and authoritative Character Catalog workflows.

## The core places designers work

| Asset or surface | What it owns | What it does not own |
|---|---|---|
| Character Profile | A character's logical animations, optional multidirectional flipbook art, per-frame hitboxes and sockets, behavior-carrying Frame Cues, curves, root motion, tags, and transition-map data | Project-wide gameplay state machines, directional playback/sequence mapping, damage authority, or persistent actor state |
| Character Layer Asset | Layered art and layer-specific authored data | Project-wide character relationships or AI decisions |
| Effect Profile | An ordered, character-agnostic library of effect flipbooks plus Type and Descriptor tags | Spawn placement, gameplay effects, runtime lookup by an effect name, or Character ownership |
| Combat Profile | Tactical attack tuning and utility-score explanations derived from one Character Profile | Physical hitbox facts, attack playback, damage, or authoritative AI state |
| Character Catalog | The project's saved character roster, companion assignments, requirements, tags, explicit groups, and base/per-group expected Animation Tags | Actor classes, spawning, encounter selection, or per-frame gameplay state |

Cue Type Blueprints own their ordinary frame-driven behavior through `On Cue Triggered` or the Cue State Begin/Update/End events. Other Blueprints still own persistent actor/game state: they may listen after Cue behavior, decide whether to use Combat advice, load Catalog soft references, play animations, and apply project authority rules.

## Recommended first-time setup

1. Create a Character Catalog asset and choose **Make Project Catalog** from its Roster tab.
2. In the Catalog's Roster, choose **+ Add Characters…** and pick the Character Profiles this project ships. The picker defaults to showing only profiles that are not in the Catalog yet, and everything you pick is added in one undoable transaction.
3. Declare the Catalog's base expected Animation Tags and any group-specific additions in the Groups rail. These are explicit project expectations; reading coverage never adds tags automatically.
4. Select each character card and create or assign its Layer, Effect, and Combat companions as needed — **Suggest Companions** fills the unambiguous Layer/Combat slots for you. Mark only genuinely required companions as **Required**.
5. Open the Character Layer Asset, define complete Appearance Presets plus one Default Appearance, and choose one delivery mode: **Fixed / Baked** for one published selection or **Runtime Customizable** for Blueprint-driven Layer changes.
6. Open the Character Profile. Use **Expected Tags** beside Details to see which animation fills every Catalog requirement and assign missing tags from the row or by dragging them onto a Grid/List animation. Use the searchable animation picker to select an animation, then author hitboxes, root motion, and the unified Frame Cues/curves timeline in their focused tools. When a logical animation needs facing-specific art, enable its Directional Set and assign slots through the shared header wheel; gameplay data remains on the base animation. Size and place the sprite against its gameplay capsule in **Profile Tools… > Character Sizing** — no character Blueprint or PIE round trip.
7. Curate effect flipbooks in the Effect Profile's **Library**, classify them in **Details**, and keep the Effect Profile assigned on the Character's Catalog row.
8. Link the Combat Profile to its Character Profile, generate missing attack tuning rows, tune the useful exceptions, and test them in **Score Playground** and **Combat Lab**.
9. Run each asset's Validation surface, then run the Catalog's project-wide **Check Again** warnings pass. Save through Unreal's normal asset save command; use **Save Bake Set** for a canonical Layer publish consistency set. Ticking an asset's Completion criteria only reaches the Catalog's authoring-progress meters once that asset is saved.

The host project must also register the Catalog Primary Asset type (`Paper2DPlusCharacterCatalog`) in its Asset Manager/cook policy. The Catalog's **Warnings** tab reports when that project-level setup is missing; the plugin does not silently change host cook rules.

## Where do I perform this task?

| I want to... | Open... |
|---|---|
| Find an animation in a large character | Character Profile, then the searchable animation picker; pin the navigator only when it helps |
| Size and place the character sprite against its gameplay capsule | Character Profile > **Profile Tools…** > Character Sizing |
| Re-cut an animation's sprites after a bad extraction | Character Profile > **Profile Tools…** > Re-extract |
| Find or fill a Catalog-required animation tag | Character Profile > Expected Tags; pick an animation from the row or drag the tag onto a Grid/List animation |
| Author combo or locomotion transition structure | Character Profile > Animations > Map |
| Assign or preview facing-specific art for one logical animation | Character Profile > Animations > Directional Animation, then the shared header wheel in any animation tool |
| Place a sound, VFX request, projectile request, or gameplay window on a key frame | Character Profile > Frame Cues |
| Make a reusable custom timing payload | A Cue or Cue State Blueprint, then place it from **+ Add Cue** |
| Decide what a Cue does in game | The Cue or Cue State Blueprint itself; implement its Cue behavior event |
| React elsewhere after a Cue runs | **Listen for Frame Cue** or the Character Profile Component's `OnFrameCue` delegate |
| Curate and search available effect art | Effect Profile > Library |
| Choose the exact effect used by one animation | An ordinary variable on the Cue Type you author; use a scalar soft Paper Flipbook when it should be warmed before frame traversal |
| Tune which attack AI should prefer | Combat Profile > Setup / Score Playground |
| See physical reach and collision without changing gameplay | Combat Profile > Attack Catalog / Combat Lab |
| Associate Character, Layer, Effect, and Combat assets | Character Catalog > Roster |
| Build explicit ordered rosters such as enemies, bosses, or civilians | Character Catalog > Roster, then its Groups rail |
| Classify characters for queries | Character Catalog row Tags |
| Organize layer-specific art, Hitboxes, sockets, and Frame Cues | Character Layer Asset > Layer Workspace |
| Publish one fixed appearance into the existing flipbooks | Character Layer Asset > Layer Workspace > bottom-right **Bake** > Adopt/Bake/Save Bake Set |
| Support Blueprint-driven appearance changes | Character Layer Asset > Appearance Presets and optional Exclusive Groups, then the actor's Layer Render component |

## Sizing a character against its capsule

Occasional-use Character Profile tools live in one **Profile Tools…** window, opened from the Character Profile editor's Asset menu or by right-clicking the profile in the Content Browser. Its left-hand list holds Character Data (the profile's Relative Transform rows for typing exact values, plus a sprite-bounds summary), Character Sizing, Sprite Bounds, Re-extract, Validation, and — when PaperZD is installed — PaperZD Sequences; they stay out of the profile editor's tab set so that editor stays sleek.

**Character Sizing** is the taught way to size and place a character's sprite. The old round trip — building a character Blueprint, adding a flipbook component, eyeballing scale in a viewport, and hand-copying the numbers back into the profile — is retired: the profile's **Relative Transform** is authored here, and nothing about sizing needs a Blueprint, a spawned actor, or PIE.

- The canvas draws one deliberate **reference pose** against the gameplay capsule and a ground line. The reference pose defaults to the idle animation and is overridable per profile; the surface always names the pose it measured. Fitting never uses max extents across every animation — one outstretched attack pose would shrink the idle, and the idle is what the character looks like standing still.
- The capsule is read from the profile's configured character class, straight off the class defaults — nothing is spawned and PIE never runs. Projects that do not use capsule characters set a target height in Unreal units instead.
- **Fit to Capsule** computes a starting transform from the reference pose's measured silhouette, its pixels-per-unit, and the capsule: uniform scale, feet on the ground line. When an input is missing it refuses with a stated reason rather than writing a degenerate transform.
- Then drag to finish: the sprite body moves it, a corner handle scales it uniformly — the character is never stretched — and arrow keys nudge. The readout shows the resulting height in Unreal units and metres, live while dragging. A whole drag is one undo entry, a click that never moves writes nothing, and Esc cancels the drag and restores the pre-drag transform without leaving an undo entry.

The sprite extractor states the same pixel-to-world relationship when it extracts (`64x64 px · 64x64 uu @ 1 px/uu`), always naming the pixels-per-unit it used, so the sizing step is arithmetic you have already seen.

### Applying the profile's Relative Transform at runtime

By default the plugin never moves the sprite: the authored Relative Transform is data, and your project applies it — typically by reading the profile's `GetRelativeTransform` in its spawn logic. Enable **Apply Profile Relative Transform** on the `Character Profile Component` to make the profile the runtime authority instead: the component places the resolved flipbook component at BeginPlay and again on late profile or flipbook assignment — always after that assignment's frame-zero Cue dispatch — so a profile handed over after spawn is placed exactly like one set on the archetype.

- It defaults to off deliberately. A project that already applies the value in its own spawn code would otherwise place the sprite twice, and a silently doubled offset is expensive to trace. Enable it on new projects, or after deleting your project-side apply; keep it off while your game owns placement — never run both.
- The apply is an absolute assignment, not an accumulated offset, so re-running it is harmless and it cannot stack on top of a game-side apply of the same values. Turning it back off restores the transform the component itself was authored with.
- When the flipbook component is the actor's root, moving it moves the whole actor and can fight replicated movement. For networked actors, prefer a flipbook component parented under the root.

## Re-extraction is the repair path

If extraction cut an animation's sprites wrong, you are not stuck with what you got. **Re-extract**, in the same Profile Tools window, re-cuts sprites from their source texture on demand using the detection settings stored when they were first extracted, without touching the file on disk — the same rollback-safe pipeline that runs automatically when a watched source texture changes on disk. **Sprite Bounds** reports; **Re-extract** fixes: an animation flagged in Sprite Bounds routes straight into this tool.

- Tick the animations to re-cut and choose **Re-extract Selected**. Each animation runs on its own, as its own transaction, so one failure cannot leave the rest half-applied; a failed animation is left exactly as it was, and every row states its outcome.
- Eligibility is explicit. An animation with no recorded source texture, or with missing or incomplete stored detection settings, is disabled with the reason written on its row — it never silently no-ops or half-applies. Re-extract such art from the sprite extractor instead.
- The run is conservative: when re-detection shifts a frame's content beyond a small tolerance, that animation refuses and reports rather than applying a suspect layout. Editing the texture on disk still triggers the automatic reimport, which walks through per-frame Accept New / Keep Current choices when that happens.
- A row warns when its sprites are shared with another Character Profile — re-extracting changes those profiles' sprites too.
- Re-extract updates sprite regions only; it never re-pads or re-trims the source texture itself.

## Empty states are instructions

- **No animation selected:** open the animation picker, search by name/tag/group, and select an animation. A pinned navigator is optional.
- **Directional slot is empty:** the preview names the exact empty sector instead of showing unrelated art. Use the selected-slot inspector to assign a compatible flipbook, choose another bearing, or clear the final slot and remove the set.
- **No Frame Cues or curves:** the timeline remains visible. Choose **+ Add Cue** or **+ Add Curve**; every animation already has an implicit Default Cue track, and optional named tracks are created through **Manage tracks...**.
- **Empty Effect Library:** choose **Add Existing Flipbooks...** or drag Paper Flipbooks from the Content Browser into the Library. Preview stays empty and the right Details panel says **No effect selected** until you choose a row.
- **No Combat attacks:** link a Character Profile and tag its attack moves, or add an explicit tuning row for an untagged move.
- **Character Sizing says `Not configured`:** the profile names no character class with a capsule and no target height. Set one of them in the profile's Character Sizing settings; until then the canvas draws a placeholder capsule and **Fit to Capsule** stays disabled.
- **Catalog says [LOCAL]:** choose **Make Project Catalog** before relying on Blueprint defaults, the Catalog pin picker, or project-wide warnings.
- **Catalog roster is empty:** clear filters, check the Groups rail scope, then choose **+ Add Characters…**. Nothing is added until you pick it.
- **A card reads `Progress unknown`:** its assets have not been saved since their Completion criteria were ticked. Tick and save each asset; the roster reads saved checklist metadata without loading anything.

## Compact workspace behavior

Paper2DPlus uses the Character Profile's quiet, focused grammar across the Layer, Effect, Combat, and Catalog editors. Narrow windows wrap or collapse secondary structure while preserving the current work; wide windows add breathing room instead of adding metadata. First-run assets keep their eventual workspace regions in place and put the next useful action inside the empty state. Validation is a compact action that opens the shared modeless issue panel, never a permanent tab.

The Character Profile, Character Layer, Effect Profile, and Combat Profile editors also keep Unreal's standard top asset toolbar. Use its Play/PIE group to start, pause/resume, step, or stop the game without leaving the active profile; these project-session controls are separate from any local preview playback inside a tool.

### Canonical flipbook-card contract — no local exceptions

Every Paper2DPlusEditor card that represents a Character Profile, flipbook, Effect library item, or Combat attack **must use `SProfileCard`**. The shared widget is the only owner of the rounded shell, selection accent, clipping, and content padding. Grid consumers must read the canonical 150-pixel width and 64-pixel preview from `SProfileCard`; compact grid cards must also read its 142-pixel compact height. Catalog and Combat tile rows must use the slab-free Content Browser tile-row style so no second black/default box is painted behind the card.

Workspace-specific labels, badges, tooltips, controls, and body arrangement belong inside the shared shell. A documented layout role may need more content space—for example the Character animation card's tag row or Effect's horizontal library row—but that is **not** permission to copy the shell, hard-code competing metrics, override its padding/selection colors, or add a consumer-only backing slab. If a new card requirement cannot fit this contract, change `SProfileCard` and its shared tests/docs for every consumer; never create a local visual exception.

- Catalog cards always show only art, designer name, one compact line, and a thin authoring-progress bar. The line is the `N/M authored` progress label unless a real problem outranks it. Full tags, paths, requirements, and relationships stay in the tooltip or right Details pane.
- Layer keeps publishing out of the top workspace: one green **Bake** button at the bottom right opens status, diagnostics, and the currently valid publish actions on demand.
- Effect keeps Library / Preview / right Details at 36% / 38% / 26%, uses the Character-style frame strip, and has no transport bar, autoplay, or draggable playback slider. **Open Flipbook…** is the only visible Preview button.
- Frame Cues keeps one timeline visible before the first item exists. Its Default and optional named Cue tracks share one frame axis with Profile-owned curves inline below; named tracks organize authoring and never change runtime order.
- At the release narrow width, Layer hides its separate Structure tree but keeps layer access, canvas, and inspector usable; Effect and Catalog preserve their primary left-to-right ownership; Combat keeps compact Setup beside the active analysis surface.
- Empty Layer, Effect, Combat, and Catalog assets explain the prerequisite or first action rather than showing blank dashboards.

## Blueprint handoff checklist

- Add a `Character Profile Component` to actors that consume Character Profile data.
- Decide who places the sprite. Either your spawn logic reads the profile's `GetRelativeTransform` and applies it, or you enable **Apply Profile Relative Transform** on the component and delete the project-side apply — never both, or the sprite is placed twice.
- For facing-specific art, call **Has Multi Direction** and **Resolve Directional Flipbook** with the Profile, the base or an owned variant, and a non-zero `Vector2D`. A populated set fails explicitly when the exact sector is empty; it never chooses a nearby slot or base art, but the failure reports which sector resolved so your own fallback can act on it. The resolver also returns the slot's **Mirror Horizontally** flag — apply the flip in your game (typically actor scale) so five authored facings can serve a full 8-way set. **Get Direction Settings**, **Get Occupied Direction Slot Indices**, **Resolve Direction Slot Index**, and **Make Direction From Bearing** are the load-free topology reads behind such a fallback. **Get Occupied Direction Slots** (callable — it loads) is the deliberate broad query for tools that need every authored index/flipbook pair, and doubles as the preload step. Project code owns playback and any resolved-flipbook-to-PaperZD-sequence mapping.
- For animation-driven combat, enable **Auto Hit Detection** on the component and bind the events instead of polling: the authored attack boxes decide *when* — entering a key frame that carries attack boxes opens the window (`OnAttackWindowBegin`), the plugin queries and dedupes automatically (once per victim per hit window; author a `HitWindow` curve for multi-hit moves), and `OnHitConnected` (attacker) / `OnHitReceived` (victim) deliver everything needed to apply damage — victim, damage, knockback, boxes, hit location, move name, frame, window. `OnAttackWindowEnd` and `OnAttackWhiffed` (armed but landed nothing) cover trails and whiff-punish logic. The plugin never applies damage; your receiver does. The manual query nodes (`Check Attack Collision`, `Quick Hit Check`, `Validate And Register Hit`) remain for off-animation cases — traps, projectiles, scripted hits.
- Use **Listen for Frame Cue** with a literal Cue class for typed Triggered/Began/Updated/Ended outputs. A Cue Type may implement its own stateless behavior before that listener broadcast; receivers still own persistent state and teardown.
- Add ordinary variables for the exact art and placement data a Cue behavior needs. Prefer a scalar soft Paper Flipbook for art that must be warmed before traversal. Do not resolve a Catalog, Effect Profile, or effect row name during Cue dispatch.
- Add a `Combat Profile Component` when an actor needs utility scoring. Supply runtime context, inspect the decision or ranked options, then let game logic decide whether and how to play the move.
- Obtain the default Catalog as a soft reference, async-load it through Unreal's normal asset loading flow, then use its pure tag/group/completion queries. Catalog queries never spawn or select actors.
- In multiplayer, run gameplay-mutating Cue receivers and gameplay decisions on authority. Use cosmetic Cue policies only for presentation.

See [Multidirectional Animations](directional-animations-guide.md), [Character Layer Authoring](layer-authoring-guide.md), [Runtime Appearance](runtime-appearance-guide.md), [Frame Cues](frame-cues-guide.md), [Effect Profiles](effect-profile-guide.md), [Combat Profiles](combat-profile-guide.md), and [Character Catalogs](character-catalog-guide.md) for the complete workflows.

## Authoring, validation, and warnings are different

| Command | Reads | Writes |
|---|---|---|
| Asset Validation | The selected asset's current authored state | Nothing |
| Project Validation (`scripts/validate-paper2dplus.ps1`) | Shared validators plus configured Catalog authority | Nothing; schema-v2 JSON and a nonzero exit when Errors exist |
| Catalog **+ Add Characters…** / **Remove from Catalog…** | The Character Profiles you pick, or the cards you selected | The Catalog only, one undoable transaction each |
| Catalog **Suggest Companions** | Inward Character relationships of candidate companions | The Catalog only, and only into empty Layer/Combat slots it can resolve uniquely |
| Catalog **Check Again** | The saved roster and the assets its entries name | Nothing; it may load those named assets for deep validation |

Warnings are not hidden behind color alone. The editors provide text status, tooltips, validation rows, and issue navigation. Search and filters do not change the current selection. Ordinary edits participate in Unreal undo/redo.

Project validation also rejects an unset, missing, or wrong-class configured default Catalog. It never chooses a replacement, writes project settings, or edits the Catalog's roster. Run the unfiltered wrapper for release; use `-Type`/`-Path` only for deliberate focused checks.

There is no automation command that reconciles the Catalog's roster: the roster is authored directly in the editor, so the automatable Catalog gates are project validation and the cook proof (`scripts/prepare-paper2dplus-character-catalog.py` plus `scripts/audit-paper2dplus-character-catalog-cook.ps1`).

Fixed publishing is exact and fail-closed. If a source uses a custom material, transformed `Texture.Source`, unsupported topology, alternate section, fractional placement, or incompatible opacity, fix the source or use Runtime Customizable delivery instead of accepting an approximation. Bake and Save Bake Set are separate explicit actions; retired/frozen outputs remain in the save scope until both the ownership-metadata changes and the final Layer checkpoint are durable.

## Migration vocabulary

Current authoring uses **Frame Cue**, **Effect Library**, and the focused Combat sections (**Variables**, **Tag Defaults**, **Scoring Profiles**, and **Scenario Presets**). Old assets may still contain deprecated executable Frame Event payloads, profile/name-based effect references, old effect categories, or name-only Combat rows. Those names are migration inputs only, not current workflow concepts.

- **v8.0.0 removed every legacy conversion.** Legacy timing payloads and legacy `Effects` rows are retained, hidden, and inert: they are never dispatched and never converted. Author a replacement Cue Type and re-place it. Nothing is deleted from your assets.
- v8.0.0 also deleted the six built-in Cue types (Play Sound, Spawn Effect, Spawn Projectile, Camera Shake, Screen Flash, Apply Gameplay Tag) with no migration and no redirect; existing placements of them stop firing and must be re-authored as Cue Types.
- An old effect category is preserved as **Legacy Category Awaiting Remap** until a designer assigns a current Type/Descriptor and clears the retained value.
- Old Combat move names are refreshed toward canonical flipbook identity when unambiguous; ambiguous or missing rows remain validation errors rather than guessing.
- Older inward Character relationships on companion assets are read only by the Catalog's explicit **Suggest Companions** action. The Catalog row is the relationship authority, and nothing reseeds a slot a designer cleared.

## Accessibility and large libraries

- Use search and tag filters before scrolling large animation, effect, attack, or character lists.
- Keep the animation navigator unpinned when the central workspace needs the room; pin it when repeatedly switching among many animations.
- Statuses include text such as `[OK]`, `[!]`, `[?]`, and `[X]` in addition to color.
- The unified Frame Cue/curve timeline keeps Cue tracks and inline curves on one frame axis. Cue placement supports keyboard movement, resizing, duplication, and removal; the focused guides list the shortcuts.
- Buttons and primary editor regions expose accessible labels, while validation issues provide remediation text and focus targets.

## What Paper2DPlus deliberately does not do

Paper2DPlus does not choose encounters, spawn Catalog characters, play Combat-selected attacks, turn a resolved directional flipbook into PaperZD playback, apply damage, or turn Effect Profile membership into runtime behavior. Cue Types do execute their own frame-driven behavior, while each project still owns persistent game state, networking authority, and the gameplay policies around those callbacks.
