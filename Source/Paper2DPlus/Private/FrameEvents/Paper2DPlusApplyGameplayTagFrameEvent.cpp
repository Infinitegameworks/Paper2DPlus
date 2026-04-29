// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "FrameEvents/Paper2DPlusApplyGameplayTagFrameEvent.h"
#include "Paper2DPlusCharacterProfileComponent.h"

void UPaper2DPlusApplyGameplayTagFrameEvent::OnFrameEventBegin_Implementation(
	const FPaper2DPlusFrameEventContext& Context)
{
	if (Context.ProfileComponent && !Tags.IsEmpty())
	{
		Context.ProfileComponent->OnApplyGameplayTagsRequested.Broadcast(Tags, /*bAdd=*/true);
	}
}

void UPaper2DPlusApplyGameplayTagFrameEvent::OnFrameEventEnd_Implementation(
	const FPaper2DPlusFrameEventContext& Context)
{
	if (Context.ProfileComponent && !Tags.IsEmpty())
	{
		Context.ProfileComponent->OnApplyGameplayTagsRequested.Broadcast(Tags, /*bAdd=*/false);
	}
}
