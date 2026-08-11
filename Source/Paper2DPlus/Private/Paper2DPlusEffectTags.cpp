// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusEffectTags.h"

#include "Misc/EngineVersionComparison.h"

// UE 5.0/5.1 predate the comment-bearing definition macro.
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 2)
#define P2DP_DEFINE_EFFECT_TAG(TagName, TagString, TagComment) UE_DEFINE_GAMEPLAY_TAG_COMMENT(TagName, TagString, TagComment)
#else
#define P2DP_DEFINE_EFFECT_TAG(TagName, TagString, TagComment) UE_DEFINE_GAMEPLAY_TAG(TagName, TagString)
#endif

namespace Paper2DPlusEffectTags
{
	P2DP_DEFINE_EFFECT_TAG(Effect, "Paper2DPlus.Effect", "Root for character-agnostic Effect Profile library tags.");

	P2DP_DEFINE_EFFECT_TAG(Type, "Paper2DPlus.Effect.Type", "Primary effect type. A library row chooses exactly one child of this root.");
	P2DP_DEFINE_EFFECT_TAG(Type_Weapon, "Paper2DPlus.Effect.Type.Weapon", "A weapon arc, slash, muzzle, or related weapon visual.");
	P2DP_DEFINE_EFFECT_TAG(Type_Projectile, "Paper2DPlus.Effect.Type.Projectile", "A projectile visual.");
	P2DP_DEFINE_EFFECT_TAG(Type_Impact, "Paper2DPlus.Effect.Type.Impact", "An impact, hit spark, burst, or contact visual.");
	P2DP_DEFINE_EFFECT_TAG(Type_Aura, "Paper2DPlus.Effect.Type.Aura", "An aura or persistent surrounding visual.");
	P2DP_DEFINE_EFFECT_TAG(Type_Trail, "Paper2DPlus.Effect.Type.Trail", "A trail or streak visual.");
	P2DP_DEFINE_EFFECT_TAG(Type_Environment, "Paper2DPlus.Effect.Type.Environment", "An environmental effect visual.");

	P2DP_DEFINE_EFFECT_TAG(Descriptor, "Paper2DPlus.Effect.Descriptor", "Optional descriptive dimensions combined on an effect library row.");
	P2DP_DEFINE_EFFECT_TAG(Descriptor_Poison, "Paper2DPlus.Effect.Descriptor.Poison", "Poison-themed visual.");
	P2DP_DEFINE_EFFECT_TAG(Descriptor_Fire, "Paper2DPlus.Effect.Descriptor.Fire", "Fire-themed visual.");
	P2DP_DEFINE_EFFECT_TAG(Descriptor_Ice, "Paper2DPlus.Effect.Descriptor.Ice", "Ice-themed visual.");
	P2DP_DEFINE_EFFECT_TAG(Descriptor_Electric, "Paper2DPlus.Effect.Descriptor.Electric", "Electric-themed visual.");
	P2DP_DEFINE_EFFECT_TAG(Descriptor_Healing, "Paper2DPlus.Effect.Descriptor.Healing", "Healing-themed visual.");

	P2DP_DEFINE_EFFECT_TAG(Layer, "Paper2DPlus.Effect.Layer", "Reserved root for a future layered-effect authoring contract.");
}

#undef P2DP_DEFINE_EFFECT_TAG
