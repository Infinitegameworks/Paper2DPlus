// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusCueTags.h"

#include "Misc/EngineVersionComparison.h"

// UE 5.0/5.1 predate the comment-bearing definition macro.
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 2)
#define P2DP_DEFINE_CUE_TAG(TagName, TagString, TagComment) UE_DEFINE_GAMEPLAY_TAG_COMMENT(TagName, TagString, TagComment)
#else
#define P2DP_DEFINE_CUE_TAG(TagName, TagString, TagComment) UE_DEFINE_GAMEPLAY_TAG(TagName, TagString)
#endif

namespace Paper2DPlusCueTags
{
	P2DP_DEFINE_CUE_TAG(
		Cue,
		"Paper2DPlus.Cue",
		"Root for designer-authored Frame Cue identity tags.");
	P2DP_DEFINE_CUE_TAG(
		Default,
		"Paper2DPlus.Cue.Default",
		"Replaceable identity assigned to a newly created Cue Type.");
}

#undef P2DP_DEFINE_CUE_TAG
