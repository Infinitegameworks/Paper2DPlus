// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusAnimationTags.h"
#include "Misc/EngineVersionComparison.h" // ENGINE_*_VERSION for the 5.0 macro guard

// UE 5.0/5.1 have no UE_DEFINE_GAMEPLAY_TAG_COMMENT variant (added in 5.2); fall back to the comment-less form.
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 2)
#define P2DP_DEFINE_ANIMATION_TAG(TagName, TagStr, TagComment) UE_DEFINE_GAMEPLAY_TAG_COMMENT(TagName, TagStr, TagComment)
#else
#define P2DP_DEFINE_ANIMATION_TAG(TagName, TagStr, TagComment) UE_DEFINE_GAMEPLAY_TAG(TagName, TagStr)
#endif

namespace Paper2DPlusAnimationTags
{
	P2DP_DEFINE_ANIMATION_TAG(Animation, "Paper2DPlus.Animation", "Root for per-animation category tags. One dimension per child subtree (Combat, Context); combine dimensions via the container on an animation, never via deep combo tags.");

	P2DP_DEFINE_ANIMATION_TAG(Combat, "Paper2DPlus.Animation.Combat", "Combat dimension — what kind of move this animation is.");
	P2DP_DEFINE_ANIMATION_TAG(Combat_Combo, "Paper2DPlus.Animation.Combat.Combo", "A combo/chain move.");
	P2DP_DEFINE_ANIMATION_TAG(Combat_Heavy, "Paper2DPlus.Animation.Combat.Heavy", "A heavy attack.");
	P2DP_DEFINE_ANIMATION_TAG(Combat_Light, "Paper2DPlus.Animation.Combat.Light", "A light attack.");
	P2DP_DEFINE_ANIMATION_TAG(Combat_Block, "Paper2DPlus.Animation.Combat.Block", "A block/guard move.");
	P2DP_DEFINE_ANIMATION_TAG(Combat_Grab, "Paper2DPlus.Animation.Combat.Grab", "A grab/throw move.");

	P2DP_DEFINE_ANIMATION_TAG(Context, "Paper2DPlus.Animation.Context", "Context dimension — the situational variant this animation answers. An animation with no Context tag is the default/standing variant.");
	P2DP_DEFINE_ANIMATION_TAG(Context_Airborne, "Paper2DPlus.Animation.Context.Airborne", "Performed while airborne.");
	P2DP_DEFINE_ANIMATION_TAG(Context_Crouching, "Paper2DPlus.Animation.Context.Crouching", "Performed while crouching.");
	P2DP_DEFINE_ANIMATION_TAG(Context_Swimming, "Paper2DPlus.Animation.Context.Swimming", "Performed while swimming.");
}

#undef P2DP_DEFINE_ANIMATION_TAG
