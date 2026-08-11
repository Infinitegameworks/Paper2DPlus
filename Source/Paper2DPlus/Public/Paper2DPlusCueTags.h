// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "NativeGameplayTags.h"

/**
 * Native gameplay tags for designer-authored Frame Cue identity.
 *
 * Cue is the picker root carried by UPaper2DPlusCueBase::CueTag. Default is the replaceable
 * identity stamped only onto newly created Cue Type generated defaults; it is not seeded into
 * project settings and it does not mutate any native Cue class default object.
 */
#define P2DP_DECLARE_CUE_TAG_EXTERN(TagName) extern PAPER2DPLUS_API FNativeGameplayTag TagName;

namespace Paper2DPlusCueTags
{
	P2DP_DECLARE_CUE_TAG_EXTERN(Cue);
	P2DP_DECLARE_CUE_TAG_EXTERN(Default);
}

#undef P2DP_DECLARE_CUE_TAG_EXTERN
