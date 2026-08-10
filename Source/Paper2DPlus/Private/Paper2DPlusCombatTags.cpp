// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusCombatTags.h"
#include "Misc/EngineVersionComparison.h" // ENGINE_*_VERSION for the 5.0 macro guard

// UE 5.0/5.1 have no UE_DEFINE_GAMEPLAY_TAG_COMMENT variant (added in 5.2); fall back to the comment-less form.
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 2)
#define P2DP_DEFINE_COMBAT_TAG(TagName, TagStr, TagComment) UE_DEFINE_GAMEPLAY_TAG_COMMENT(TagName, TagStr, TagComment)
#else
#define P2DP_DEFINE_COMBAT_TAG(TagName, TagStr, TagComment) UE_DEFINE_GAMEPLAY_TAG(TagName, TagStr)
#endif

namespace Paper2DPlusCombatTags
{
	P2DP_DEFINE_COMBAT_TAG(Var, "Paper2DPlus.Combat.Var", "Root namespace for Combat Profile scoring variables — pick or create children under this.");
	P2DP_DEFINE_COMBAT_TAG(Var_Aggression, "Paper2DPlus.Combat.Var.Aggression", "Starter combat scoring variable example.");
	P2DP_DEFINE_COMBAT_TAG(Var_CanPunish, "Paper2DPlus.Combat.Var.CanPunish", "Starter combat scoring variable example.");
}

#undef P2DP_DEFINE_COMBAT_TAG
