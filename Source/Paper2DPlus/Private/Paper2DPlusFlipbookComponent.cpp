// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusFlipbookComponent.h"
#include "PaperFlipbook.h"

/** UPaper2DPlusFlipbookComponent — PaperFlipbookComponent subclass that fires OnFlipbookChanged and OnFrameChanged delegates. */

bool UPaper2DPlusFlipbookComponent::SetFlipbook(UPaperFlipbook* NewFlipbook)
{
	const bool bResult = Super::SetFlipbook(NewFlipbook);
	if (bResult)
	{
		// Seed PreviousCachedFrameIndex to match CachedFrameIndex so TickComponent
		// does NOT re-broadcast OnFrameChanged for frame 0 — the OnFlipbookChanged
		// path handles initial-frame dispatch, otherwise ranged event Tick fires twice.
		PreviousCachedFrameIndex = CachedFrameIndex;
		OnFlipbookChanged.Broadcast(NewFlipbook);
	}
	return bResult;
}

void UPaper2DPlusFlipbookComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Free-skip when no consumer is bound — unbound subclass instances pay almost nothing.
	if (!OnFrameChanged.IsBound()) return;

	// Super::TickComponent updates CachedFrameIndex via CalculateCurrentFrame().
	// We only fire on valid->valid transitions; the initial frame after SetFlipbook
	// is dispatched via the OnFlipbookChanged path, not here.
	if (CachedFrameIndex != PreviousCachedFrameIndex && CachedFrameIndex != INDEX_NONE)
	{
		PreviousCachedFrameIndex = CachedFrameIndex;
		OnFrameChanged.Broadcast(CachedFrameIndex);
	}
}
