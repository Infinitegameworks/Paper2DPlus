// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "FrameEvents/Paper2DPlusScreenFlashFrameEvent.h"
#include "Paper2DPlusCharacterProfileComponent.h"

void UPaper2DPlusScreenFlashFrameEvent::OnReceiveFrameEvent_Implementation(
	const FPaper2DPlusFrameEventContext& Context)
{
	if (Context.ProfileComponent)
	{
		Context.ProfileComponent->OnScreenFlashRequested.Broadcast(FlashColor, Duration);
	}
}
