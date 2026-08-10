// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "FrameEvents/Paper2DPlusScreenFlashFrameEvent.h"

UPaper2DPlusScreenFlashFrameEvent::UPaper2DPlusScreenFlashFrameEvent()
{
	// Screen feedback is a cosmetic — networked-correct default (TASK-57 U1).
	NetPolicy = EPaper2DPlusFrameEventNetPolicy::CosmeticOnly;
}
