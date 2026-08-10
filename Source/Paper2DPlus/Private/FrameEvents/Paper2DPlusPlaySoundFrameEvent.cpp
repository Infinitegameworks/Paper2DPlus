// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "FrameEvents/Paper2DPlusPlaySoundFrameEvent.h"

UPaper2DPlusPlaySoundFrameEvent::UPaper2DPlusPlaySoundFrameEvent()
{
	// Audio is a world-audible cosmetic — networked-correct default (TASK-57 U1).
	NetPolicy = EPaper2DPlusFrameEventNetPolicy::CosmeticOnly;
}
