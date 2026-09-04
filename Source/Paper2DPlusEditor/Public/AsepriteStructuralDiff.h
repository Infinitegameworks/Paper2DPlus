// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FAsepriteParsedData;
struct FAsepriteTag;
struct FCharacterLayer;
struct FCharacterLayerAnimationMapping;
struct FCharacterLayerAppearancePreset;

/**
 * One previously-imported source item (a layer or an animation tag) as recorded by the import
 * context of ONE source .ase file. Pure data — callers derive the two policy flags from asset
 * state (authored-data detection) and from the OTHER sources' contribution records.
 */
struct FAseDiffOldItem
{
	/** Layer FULL hierarchy path, or tag name. The identity the previous import keyed assets by. */
	FString Name;

	/** Content hash stamped at the previous import (empty = legacy, never stamped — excluded from
	 *  rename pairing so a rename can only degrade to delete+add, never mispair). */
	FString ContentHash;

	/** True when engine-side data bound to this item carries authored edits — a removal must KEEP it. */
	bool bHasAuthoredData = false;

	/** True when any SIBLING source of the same Layer Profile also contributes this name. Blocks
	 *  rename pairing AND clean removal (the sibling still owns the shared data). */
	bool bSharedWithOtherSources = false;
};

/** One item present in the newly parsed .ase content. */
struct FAseDiffNewItem
{
	FString Name;
	FString ContentHash;

	/** True when this name is ALREADY owned on the asset outside the reimported source's own
	 *  contribution (a sibling source's layer/tag). Blocks rename pairing onto it: renaming would
	 *  collide with that owner's generated assets on disk, or silently merge into its data. */
	bool bCollidesWithExistingItem = false;
};

/** An accepted rename pairing: unambiguous (1:1 by content hash) and pixel-identical. */
struct FAseDiffRename
{
	FString OldName;
	FString NewName;
};

/** Whether an accepted layer rename can actually be applied to the asset in front of us. */
enum class EAseLayerRenamePlan : uint8
{
	/** Apply it: the old layer exists and nothing else owns the new name. */
	Applicable,
	/** The stamp named a layer this asset no longer holds; the ordinary add path covers it. */
	OldNameNotFound,
	/** Something already owns the new name. REFUSE — renaming onto it would collide on disk or
	 *  silently merge two layers' authored data. The caller degrades to delete+add and reports. */
	NewNameAlreadyOwned
};

/** What a non-renamed removed item should do. */
enum class EAseDiffRemovalDisposition : uint8
{
	/** No authored data, not shared: generated data may be removed (assets stay on disk as orphans). */
	RemoveClean,
	/** Carries authored edits: KEEP the data and report (never destroy authored work unattended). */
	KeepAuthoredData,
	/** A sibling source still contributes this name: KEEP and report as owned by that source. */
	KeepSharedWithOtherSource
};

struct FAseDiffRemoval
{
	FString Name;
	EAseDiffRemovalDisposition Disposition = EAseDiffRemovalDisposition::RemoveClean;
};

/** Structural diff of one source's previous contribution against its newly parsed content. */
struct FAseDiffResult
{
	/** Names present on both sides (case-insensitive) — the ordinary refresh set. */
	TArray<FString> MatchedNames;

	/** Accepted rename pairings (see DiffItems for the exact rule). */
	TArray<FAseDiffRename> Renames;

	/** Removed items that did NOT pair as renames, each with its keep/remove disposition. */
	TArray<FAseDiffRemoval> Removals;

	/** New items that did NOT pair as renames. */
	TArray<FString> Added;
};

/**
 * What one structural-diff apply pass actually did, for the NON-MODAL report (R13). Every field is
 * a human-readable line the caller logs and summarises; nothing here is consumed as control flow.
 * An apply that changed nothing leaves this empty, which is how a true no-op reimport stays silent
 * AND undirtied.
 */
struct FAseDiffApplyReport
{
	/** Renames actually applied, in place, with their generated assets moved on disk. */
	TArray<FAseDiffRename> LayerRenames;
	TArray<FAseDiffRename> TagRenames;

	/** Pairings the apply layer REFUSED after the pure diff accepted them (a collision the disk or
	 *  the profile's own rename funnel rejected). Each line names the pair and the reason. */
	TArray<FString> DegradedRenames;

	/** Removed-from-source items KEPT because they carry authored edits or a sibling source owns them. */
	TArray<FString> KeptItems;

	/** Removed-from-source items whose purely generated data WAS dropped. */
	TArray<FString> RemovedItems;

	/** Generated packages nothing references any more. REPORTED, never deleted (R11). */
	TArray<FString> OrphanedAssets;

	/** Default-Appearance joins refused because the layer's Exclusive Group was already occupied. */
	TArray<FString> PresetJoinRefusals;

	/** Merge bookkeeping accumulated across every matched layer. */
	int32 MappingsRefreshed = 0;
	int32 MappingsAdded = 0;

	bool IsEmpty() const
	{
		return LayerRenames.Num() == 0 && TagRenames.Num() == 0 && DegradedRenames.Num() == 0
			&& KeptItems.Num() == 0 && RemovedItems.Num() == 0 && OrphanedAssets.Num() == 0
			&& PresetJoinRefusals.Num() == 0;
	}

	/** One-line human summary for the editor notification; empty when nothing happened. */
	FString ToSummaryText() const;

	/** Every decision as its own line, for the log. */
	void AppendDetailLines(TArray<FString>& OutLines) const;
};

/**
 * Pure structural-diff and merge cores for the .ase reimport pipeline (TASK-189). Worldless by
 * design: no Slate, no packages, no asset loads — the apply layer in FAsepriteImporter feeds these
 * from asset state and applies the decisions.
 */
class PAPER2DPLUSEDITOR_API FAsepriteStructuralDiff
{
public:
	/**
	 * PURE: diff one source's previously recorded items against the newly parsed set.
	 *
	 * Matching is by name, case-insensitive (the pipeline's existing layer/tag matching rule);
	 * duplicate names within one side collapse to their first occurrence. A rename pairs ONLY when
	 * ALL hold (the fail-closed R9 rule):
	 *   - the removed item's hash is non-empty and equals the added item's hash (pixel-identical);
	 *   - that hash maps to EXACTLY ONE removed item and EXACTLY ONE added item (unambiguous, 1:1);
	 *   - the removed item is not shared with a sibling source (bSharedWithOtherSources == false);
	 *   - the added item's name is not already owned elsewhere on the asset
	 *     (bCollidesWithExistingItem == false).
	 * Anything else degrades to delete+add: the removed item lands in Removals with a disposition
	 * (shared beats authored beats clean), the added item lands in Added. Deterministic: outputs
	 * follow the input arrays' order.
	 */
	static FAseDiffResult DiffItems(
		const TArray<FAseDiffOldItem>& OldItems,
		const TArray<FAseDiffNewItem>& NewItems);

	/**
	 * PURE: per-animation union of a matched layer's AnimationSprites — the multi-file-one-Layer-
	 * Profile merge that replaces the wholesale array assignment. For each incoming mapping, an
	 * existing mapping with the same AnimationName (case-insensitive) is REFRESHED in place
	 * (AnimationName + Sprites adopted; an existing editor-only Flipbook binding survives when the
	 * incoming one is null); anything else is APPENDED. Existing mappings the incoming set does not
	 * name are KEPT untouched — another source's contributions survive by construction.
	 *
	 * A refresh whose sprites (by soft object path, in order) and name are already identical is a
	 * no-op and does not count. Returns true when anything actually changed, so a true no-op
	 * reimport can skip Modify()/dirty entirely (R14).
	 *
	 * ORDER CONTRACT: existing entries never move; appends follow the INCOMING array's order. Two
	 * merges of the same incoming set in different orders therefore produce the same entry SET and
	 * the same counts, but not necessarily the same append order.
	 */
	static bool MergeAnimationSprites(
		TArray<FCharacterLayerAnimationMapping>& Existing,
		const TArray<FCharacterLayerAnimationMapping>& Incoming,
		int32* OutRefreshed = nullptr,
		int32* OutAdded = nullptr);

	/**
	 * PURE (R12): join each named NEW layer into Preset's ActiveLayerIds so a layer added by a
	 * reimport is visible in the Default Appearance instead of silently invisible.
	 *
	 * FAILS CLOSED on Exclusive Groups: a preset holding two members of one group is invalid, so a
	 * new layer whose ExclusiveGroupId already has a member active in Preset is REFUSED (reported
	 * through OutRefusedLayerNames) rather than joined. Unknown names and layers already present in
	 * the preset are skipped silently. Group occupancy is evaluated against the preset's LIVE
	 * contents, so two new members of the same group cannot both join in one pass. Returns the
	 * number of layers actually joined (0 = Preset untouched).
	 */
	static int32 JoinNewLayersIntoPreset(
		const TArray<FCharacterLayer>& Layers,
		const TArray<FString>& NewLayerNames,
		FCharacterLayerAppearancePreset& Preset,
		TArray<FString>* OutRefusedLayerNames = nullptr);

	/**
	 * PURE (R10): does removing this layer discard curated work? True when the layer carries a
	 * group binding, authored animations, a non-zero placement offset, per-animation offsets, a
	 * non-default composition mode, an Exclusive Group binding, or membership in ANY appearance
	 * preset. A true answer means KEEP AND REPORT — never destroy authored work unattended.
	 *
	 * This is the hoisted, parameter-cleaned twin of FAsepriteReimporter::HasManualEdits (whose
	 * LayerIndex/TotalLayers arguments were never read); the legacy reimporter forwards to it so
	 * both reimport paths answer the question identically.
	 */
	static bool HasAuthoredLayerData(
		const FCharacterLayer& Layer,
		const TArray<FCharacterLayerAppearancePreset>& Presets);

	/**
	 * PURE (R10): does this ANIMATION carry authored edits on the layer-asset side — an authored
	 * gameplay row or a per-animation placement offset on any layer? Generated sprite mappings
	 * alone are NOT authored data. Matching is case-insensitive.
	 */
	static bool HasAuthoredAnimationData(
		const TArray<FCharacterLayer>& Layers,
		const FString& AnimationName);

	/**
	 * PURE (R9): can this accepted layer rename be applied in place, and to which layer?
	 *
	 * The layer twin of RenameAnimationOnLayers' collision refusal, hoisted out of the apply layer so
	 * the DECISION is testable without an import. It deliberately does NOT perform the rename: the
	 * caller still has work to do between deciding and writing (deriving the new output names), and
	 * an early return in that stretch must not leave a half-renamed layer behind.
	 *
	 * WHY THIS IS ORDER-SENSITIVE, and the reason the apply layer runs BEFORE the pipeline writes:
	 * every creator is FindOrCreate-in-place, so once the creation pass has minted a layer under the
	 * NEW name this returns NewNameAlreadyOwned and the rename degrades to delete+add — the old layer
	 * strands and its authored data is left behind on a name nothing points at any more.
	 *
	 * Matching is case-insensitive (the pipeline's layer-matching rule).
	 */
	static EAseLayerRenamePlan PlanLayerRename(
		const TArray<FCharacterLayer>& Layers,
		const FString& OldName,
		const FString& NewName,
		int32* OutLayerIndex = nullptr);

	/**
	 * PURE (R9): re-key one animation name across every layer-asset collection that the profile's
	 * own rename funnel deliberately excludes — AnimationSprites keys, per-animation offsets, and
	 * authored gameplay rows. Case-insensitive match, exact new spelling written. Returns the row
	 * count re-keyed (0 = nothing on this asset referenced the old name).
	 *
	 * A rename that would collide with an existing row of the NEW name on the same layer is
	 * refused for that layer (the collision is reported by the caller and degrades the pairing),
	 * so a rename can never silently merge two animations' authored data.
	 */
	static int32 RenameAnimationOnLayers(
		TArray<FCharacterLayer>& Layers,
		const FString& OldAnimationName,
		const FString& NewAnimationName,
		TArray<FString>* OutCollidedLayerNames = nullptr);

	/**
	 * PURE (R10): drop one animation's GENERATED rows (sprite mappings only) from every layer.
	 * Authored offsets and authored gameplay rows are left alone — callers only reach this after
	 * HasAuthoredAnimationData said the animation is unedited. Returns the row count removed.
	 */
	static int32 RemoveAnimationFromLayers(
		TArray<FCharacterLayer>& Layers,
		const FString& AnimationName);

	/**
	 * PURE (R10): drop a layer by name and every appearance preset's membership of it. Returns
	 * true when a layer was removed, writing its stable id to OutRemovedLayerId. Presets are
	 * cleaned in the same pass so a preset can never keep a dangling LayerId.
	 */
	static bool RemoveLayerByName(
		TArray<FCharacterLayer>& Layers,
		TArray<FCharacterLayerAppearancePreset>& Presets,
		const FString& LayerName,
		FGuid* OutRemovedLayerId = nullptr);

	/**
	 * PURE: content hash (MD5 hex) of ONE layer's per-frame composited buffers — the layer's OWN
	 * cel pixels only, so edits to other layers never perturb it. Frame-set changes (count/dims)
	 * change the hash by design. Empty input hashes to a stable non-empty digest.
	 */
	static FString ComputeLayerBuffersHash(
		const TArray<TArray<FColor>>& FrameBuffers, int32 FrameWidth, int32 FrameHeight);

	/**
	 * PURE: content hash (MD5 hex) of a tag's composited full frames (Data.Frames pixels over the
	 * tag's authored [From..To] range, ascending, clamped). Durations are deliberately EXCLUDED —
	 * the pairing rule is pixel identity, and a retimed-but-identical tag must still pair.
	 */
	static FString ComputeTagFramesHash(const FAsepriteParsedData& Data, const FAsepriteTag& Tag);
};
