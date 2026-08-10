// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "NativeGameplayTags.h"

/**
 * Native gameplay tags for the Combat Profile scoring system.
 *
 * `Var` is the root namespace combat scoring variables live under — designers pick or create children
 * (e.g. Paper2DPlus.Combat.Var.Aggression) via the tag picker on FPaper2DPlusCombatVariableDefinition.
 * `FPaper2DPlusCombatVariableDefinition::VariableTag` (and a consideration's VariableTag) reference these,
 * so variable identity stays searchable and rename-safe instead of a free-form FName.
 *
 * A couple of starter variables ship as native tags so a fresh project has examples to extend and the
 * worldless combat tests have registered tags to reference.
 */
namespace Paper2DPlusCombatTags
{
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Var);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Var_Aggression);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Var_CanPunish);
}
