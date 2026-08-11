// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "NativeGameplayTags.h"

/**
 * Native gameplay tags for the character-agnostic Effect Profile library.
 *
 * Every library row has one primary `Paper2DPlus.Effect.Type.*` tag and may have any number of
 * `Paper2DPlus.Effect.Descriptor.*` tags. Designers may extend both subtrees. The Layer branch is
 * intentionally only a reserved root until layered-effect authoring receives its own data contract.
 */
#define P2DP_DECLARE_EFFECT_TAG_EXTERN(TagName) extern PAPER2DPLUS_API FNativeGameplayTag TagName;

namespace Paper2DPlusEffectTags
{
	P2DP_DECLARE_EFFECT_TAG_EXTERN(Effect);

	P2DP_DECLARE_EFFECT_TAG_EXTERN(Type);
	P2DP_DECLARE_EFFECT_TAG_EXTERN(Type_Weapon);
	P2DP_DECLARE_EFFECT_TAG_EXTERN(Type_Projectile);
	P2DP_DECLARE_EFFECT_TAG_EXTERN(Type_Impact);
	P2DP_DECLARE_EFFECT_TAG_EXTERN(Type_Aura);
	P2DP_DECLARE_EFFECT_TAG_EXTERN(Type_Trail);
	P2DP_DECLARE_EFFECT_TAG_EXTERN(Type_Environment);

	P2DP_DECLARE_EFFECT_TAG_EXTERN(Descriptor);
	P2DP_DECLARE_EFFECT_TAG_EXTERN(Descriptor_Poison);
	P2DP_DECLARE_EFFECT_TAG_EXTERN(Descriptor_Fire);
	P2DP_DECLARE_EFFECT_TAG_EXTERN(Descriptor_Ice);
	P2DP_DECLARE_EFFECT_TAG_EXTERN(Descriptor_Electric);
	P2DP_DECLARE_EFFECT_TAG_EXTERN(Descriptor_Healing);

	P2DP_DECLARE_EFFECT_TAG_EXTERN(Layer);
}

#undef P2DP_DECLARE_EFFECT_TAG_EXTERN
