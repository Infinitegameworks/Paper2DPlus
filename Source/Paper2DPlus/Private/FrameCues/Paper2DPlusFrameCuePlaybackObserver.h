// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Paper2DPlusFrameCuePlaybackObserver.generated.h"

class UPaper2DPlusCharacterProfileComponent;
class UPaperFlipbookComponent;

/**
 * Sender-bearing bridge for UPaperFlipbookComponent's senderless dynamic finish delegate.
 *
 * A separate observer is created for each stock-component binding. If an old multicast invocation
 * survives delegate removal while the Profile Component rebinds, this object still reports the old
 * source and epoch instead of mislabeling that callback as completion of the replacement source.
 */
UCLASS(Transient)
class UPaper2DPlusFrameCueStockPlaybackObserver final : public UObject
{
	GENERATED_BODY()

public:
	void Initialize(
		UPaper2DPlusCharacterProfileComponent* InOwner,
		UPaperFlipbookComponent* InSource,
		uint64 InBindingEpoch)
	{
		Owner = InOwner;
		Source = InSource;
		BindingEpoch = InBindingEpoch;
	}

	void Detach()
	{
		Owner = nullptr;
		Source = nullptr;
		BindingEpoch = 0;
	}

	UFUNCTION()
	void HandleFinishedPlaying();

private:
	UPROPERTY(Transient)
	TObjectPtr<UPaper2DPlusCharacterProfileComponent> Owner = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UPaperFlipbookComponent> Source = nullptr;

	uint64 BindingEpoch = 0;
};
