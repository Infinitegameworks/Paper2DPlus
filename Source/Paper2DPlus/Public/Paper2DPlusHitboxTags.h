// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "NativeGameplayTags.h"

/**
 * Native gameplay tags for the hit-priority / clash-resolution system (TASK-77).
 *
 * `Category` is the root namespace clash categories live under. Designers pick or create children
 * (e.g. Paper2DPlus.Clash.Category.Strike.Heavy) via the tag picker on a hitbox's ClashCategory and on a
 * UPaper2DPlusClashGraphAsset edge. Categories are HIERARCHICAL: a sub-tag inherits its parent's clash
 * relationships (a `Strike` "beats" edge applies to `Strike.Heavy` via MatchesTag), and a more-specific
 * edge overrides the parent's — resolved by Paper2DPlusClash::ResolveClash, validated by ValidateClashGraph.
 *
 * The 6 genre categories + a couple of seed sub-tags ship as native tags so a fresh project has the
 * conventional fighting-game taxonomy to extend, and the worldless clash tests have registered
 * hierarchical tags to reference.
 */
namespace Paper2DPlusHitboxTags
{
	// Root.
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Category);

	// The 6 genre categories (strikes / throws / projectiles / armor / invincible / parry).
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Category_Strike);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Category_Throw);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Category_Projectile);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Category_Armor);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Category_Invincible);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Category_Parry);

	// Seed sub-tags so hierarchy resolution has registered examples (Strike.Heavy beats Strike.Light, etc.).
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Category_Strike_Heavy);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Category_Strike_Light);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Category_Projectile_Arrow);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Category_Projectile_Magic);
}
