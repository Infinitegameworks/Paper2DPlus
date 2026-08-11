// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FPaper2DPlusCombatConsideration;

namespace Paper2DPlusCombatText
{
	/**
	 * Plain-English reading of a scoring rule, e.g. "When distance to target is within 100-300,
	 * multiply score (x1.0)". Shared by the consideration property customization (collapsed header)
	 * and the per-attack "effective rules" hierarchy view so the two never phrase a rule differently.
	 */
	FText SummarizeConsideration(const FPaper2DPlusCombatConsideration& Consideration);
}
