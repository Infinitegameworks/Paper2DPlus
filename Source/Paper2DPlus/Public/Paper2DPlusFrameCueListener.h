// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "Templates/SubclassOf.h"
#include "FrameCues/Paper2DPlusFrameCue.h"
#include "Paper2DPlusFrameCueListener.generated.h"

class UPaper2DPlusCharacterProfileComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FPaper2DPlusFrameCueListenerOutput,
	UPaper2DPlusCueBase*, Cue, const FPaper2DPlusFrameCueContext&, Context);

/**
 * Cancellable, filtered Blueprint listener for a profile component's Frame Cues.
 * The Cue Type's own behavior runs first; use the dedicated Listen for Frame Cue node when another
 * Blueprint needs a concrete Cue output pin afterward.
 */
UCLASS(meta = (ExposedAsyncProxy = "Listener", HasDedicatedAsyncNode))
class PAPER2DPLUS_API UPaper2DPlusFrameCueListener : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable)
	FPaper2DPlusFrameCueListenerOutput Triggered;

	UPROPERTY(BlueprintAssignable)
	FPaper2DPlusFrameCueListenerOutput Began;

	UPROPERTY(BlueprintAssignable)
	FPaper2DPlusFrameCueListenerOutput Updated;

	UPROPERTY(BlueprintAssignable)
	FPaper2DPlusFrameCueListenerOutput Ended;

	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Frame Cues",
		meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject"))
	static UPaper2DPlusFrameCueListener* ListenForFrameCue(
		UObject* WorldContextObject,
		UPaper2DPlusCharacterProfileComponent* ProfileComponent,
		TSubclassOf<UPaper2DPlusCueBase> CueClass,
		FGameplayTag CueTag,
		bool bExactClass = false,
		bool bExactTag = false);

	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Frame Cues")
	void Cancel();

	virtual void Activate() override;
	virtual void BeginDestroy() override;

	private:
	UPROPERTY()
	TObjectPtr<UObject> WorldContext = nullptr;

	UPROPERTY()
	TObjectPtr<UPaper2DPlusCharacterProfileComponent> SourceComponent = nullptr;

	UPROPERTY()
	TSubclassOf<UPaper2DPlusCueBase> FilterClass;

	UPROPERTY()
	FGameplayTag FilterTag;

	bool bFilterExactClass = false;
	bool bFilterExactTag = false;
	bool bListening = false;
	FDelegateHandle SourceEndedHandle;
	FDelegateHandle WorldCleanupHandle;

	UFUNCTION()
	void HandleCue(UPaper2DPlusCueBase* Cue, const FPaper2DPlusFrameCueContext& Context);

	void HandleSourceEnded();
	void HandleWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);
	bool PassesFilter(const UPaper2DPlusCueBase& Cue) const;
	void Unbind();
};
