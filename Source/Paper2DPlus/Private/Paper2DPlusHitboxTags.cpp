// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusHitboxTags.h"
#include "Misc/EngineVersionComparison.h" // ENGINE_*_VERSION for the 5.0/5.1 macro guard

// UE 5.0/5.1 have no UE_DEFINE_GAMEPLAY_TAG_COMMENT variant (added in 5.2); fall back to the comment-less form.
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 2)
#define P2DP_DEFINE_HITBOX_TAG(TagName, TagStr, TagComment) UE_DEFINE_GAMEPLAY_TAG_COMMENT(TagName, TagStr, TagComment)
#else
#define P2DP_DEFINE_HITBOX_TAG(TagName, TagStr, TagComment) UE_DEFINE_GAMEPLAY_TAG(TagName, TagStr)
#endif

namespace Paper2DPlusHitboxTags
{
	P2DP_DEFINE_HITBOX_TAG(Category, "Paper2DPlus.Clash.Category", "Root namespace for hit-priority clash categories — pick or create children under this.");

	P2DP_DEFINE_HITBOX_TAG(Category_Strike, "Paper2DPlus.Clash.Category.Strike", "A normal/special/super attack box (distinguish power via the clash graph + sub-tags).");
	P2DP_DEFINE_HITBOX_TAG(Category_Throw, "Paper2DPlus.Clash.Category.Throw", "A throw/grab — beats armor; never trades with a strike (whiff/dodge semantics).");
	P2DP_DEFINE_HITBOX_TAG(Category_Projectile, "Paper2DPlus.Clash.Category.Projectile", "A projectile — cancels other projectiles by durability.");
	P2DP_DEFINE_HITBOX_TAG(Category_Armor, "Paper2DPlus.Clash.Category.Armor", "An armored move — absorbs strikes/projectiles; loses to throws.");
	P2DP_DEFINE_HITBOX_TAG(Category_Invincible, "Paper2DPlus.Clash.Category.Invincible", "An invincible reversal window — beats strikes; loses to throws.");
	P2DP_DEFINE_HITBOX_TAG(Category_Parry, "Paper2DPlus.Clash.Category.Parry", "A parry/deflect — beats strikes & projectiles; loses to throws.");

	P2DP_DEFINE_HITBOX_TAG(Category_Strike_Heavy, "Paper2DPlus.Clash.Category.Strike.Heavy", "Seed sub-tag: a heavy strike (beats light strikes via the clash graph).");
	P2DP_DEFINE_HITBOX_TAG(Category_Strike_Light, "Paper2DPlus.Clash.Category.Strike.Light", "Seed sub-tag: a light strike.");
	P2DP_DEFINE_HITBOX_TAG(Category_Projectile_Arrow, "Paper2DPlus.Clash.Category.Projectile.Arrow", "Seed sub-tag: an arrow projectile.");
	P2DP_DEFINE_HITBOX_TAG(Category_Projectile_Magic, "Paper2DPlus.Clash.Category.Projectile.Magic", "Seed sub-tag: a magic projectile.");
}

#undef P2DP_DEFINE_HITBOX_TAG
