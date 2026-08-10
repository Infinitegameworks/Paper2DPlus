// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "FrameEvents/Paper2DPlusApplyGameplayTagFrameEvent.h"

UPaper2DPlusApplyGameplayTagFrameEvent::UPaper2DPlusApplyGameplayTagFrameEvent()
{
	// Gameplay tags mutate gameplay state — networked-correct default (TASK-57 U1).
	NetPolicy = EPaper2DPlusFrameEventNetPolicy::AuthorityOnly;
}
