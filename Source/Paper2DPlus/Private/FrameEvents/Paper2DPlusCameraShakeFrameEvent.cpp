// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "FrameEvents/Paper2DPlusCameraShakeFrameEvent.h"

UPaper2DPlusCameraShakeFrameEvent::UPaper2DPlusCameraShakeFrameEvent()
{
	// Camera feedback is a cosmetic — networked-correct default (TASK-57 U1).
	NetPolicy = EPaper2DPlusFrameEventNetPolicy::CosmeticOnly;
}
