// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "FrameEvents/Paper2DPlusSpawnProjectileFrameEvent.h"

UPaper2DPlusSpawnProjectileFrameEvent::UPaper2DPlusSpawnProjectileFrameEvent()
{
	// Spawning an actor mutates gameplay state — networked-correct default (TASK-57 U1).
	// Inert until the owning component opts into replication (Standalone dispatches everything).
	NetPolicy = EPaper2DPlusFrameEventNetPolicy::AuthorityOnly;
}
