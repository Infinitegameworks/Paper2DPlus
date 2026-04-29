// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PaperFlipbookComponent.h"
#include "Paper2DPlusFlipbookComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPaper2DPlusFlipbookChanged, UPaperFlipbook*, NewFlipbook);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPaper2DPlusFrameChanged, int32, NewFrameIndex);

/**
 * Drop-in replacement for UPaperFlipbookComponent that fires:
 *  - OnFlipbookChanged when SetFlipbook changes the active flipbook
 *  - OnFrameChanged when the playing flipbook advances to a new key frame
 *
 * UPaper2DPlusCharacterProfileComponent auto-detects this subclass and uses
 * both delegates for zero-overhead event-driven root motion + effect spawning
 * instead of polling its own TickComponent.
 */
UCLASS(ClassGroup=(Paper2DPlus), meta=(BlueprintSpawnableComponent, DisplayName="Paper2DPlus Flipbook Component"))
class PAPER2DPLUS_API UPaper2DPlusFlipbookComponent : public UPaperFlipbookComponent
{
	GENERATED_BODY()

public:
	/** Fired when SetFlipbook changes the active flipbook. */
	UPROPERTY(BlueprintAssignable, Category = "Paper2DPlus")
	FOnPaper2DPlusFlipbookChanged OnFlipbookChanged;

	/**
	 * Fired when the playing flipbook advances to a new key-frame index.
	 * Only fires on valid->valid transitions during playback; the initial frame
	 * after SetFlipbook is dispatched via the OnFlipbookChanged path, not here.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Paper2DPlus")
	FOnPaper2DPlusFrameChanged OnFrameChanged;

	virtual bool SetFlipbook(UPaperFlipbook* NewFlipbook) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	/** Last key-frame index observed during TickComponent. Reset on SetFlipbook. */
	int32 PreviousCachedFrameIndex = INDEX_NONE;
};
