// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "NativeGameplayTags.h"

/**
 * Native gameplay tags for per-animation categorization (TASK-108).
 *
 * These are the built-in `Paper2DPlus.Animation` taxonomy referenced by
 * `FFlipbookEditorMetadata::AnimationTags` (a container, purely descriptive — never drives playback)
 * and consumed by the animation tag queries. Designers can extend the tree with their own children
 * via the tag picker; the tags below ship natively so a fresh project has a working taxonomy and the
 * worldless tests have registered tags to reference.
 *
 * THE DIMENSION RULE (how to author under this root):
 *  - Hierarchy = SPECIALIZATION within ONE dimension. `Combat.Heavy` is a kind of `Combat`;
 *    `Context.Airborne` is a kind of `Context`. Dimensions live as SIBLING subtrees
 *    (`Animation.Combat`, `Animation.Context`, ...).
 *  - The CONTAINER = COMBINATION ACROSS dimensions. An airborne heavy is tagged
 *    {`Combat.Heavy`, `Context.Airborne`} on one animation — NEVER a deep combo tag like
 *    `Combat.Heavy.Airborne`. Deep combo tags multiply the tree and break hierarchical queries.
 *  - An animation with NO `Context.*` tag is the default/standing variant of its move.
 */
// UE_DECLARE_GAMEPLAY_TAG_EXTERN emits a bare `extern` with NO module API decoration, so the statics
// were link-visible only INSIDE the runtime module; the editor module (AnimationMapCore's chip-color
// family dispatch) now reads them too, so declare with PAPER2DPLUS_API (the standard
// `extern MODULE_API` data-export idiom). Definitions in the .cpp are unchanged.
#define P2DP_DECLARE_ANIMATION_TAG_EXTERN(TagName) extern PAPER2DPLUS_API FNativeGameplayTag TagName;

namespace Paper2DPlusAnimationTags
{
	// Root
	P2DP_DECLARE_ANIMATION_TAG_EXTERN(Animation);

	// Combat dimension — what kind of move this animation is.
	P2DP_DECLARE_ANIMATION_TAG_EXTERN(Combat);
	P2DP_DECLARE_ANIMATION_TAG_EXTERN(Combat_Combo);
	P2DP_DECLARE_ANIMATION_TAG_EXTERN(Combat_Heavy);
	P2DP_DECLARE_ANIMATION_TAG_EXTERN(Combat_Light);
	P2DP_DECLARE_ANIMATION_TAG_EXTERN(Combat_Block);
	P2DP_DECLARE_ANIMATION_TAG_EXTERN(Combat_Grab);

	// Context dimension — the situational variant this animation answers. Untagged = default/standing.
	P2DP_DECLARE_ANIMATION_TAG_EXTERN(Context);
	P2DP_DECLARE_ANIMATION_TAG_EXTERN(Context_Airborne);
	P2DP_DECLARE_ANIMATION_TAG_EXTERN(Context_Crouching);
	P2DP_DECLARE_ANIMATION_TAG_EXTERN(Context_Swimming);
}

#undef P2DP_DECLARE_ANIMATION_TAG_EXTERN
