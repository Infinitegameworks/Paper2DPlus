// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "FrameCues/Paper2DPlusFrameCue.h"
#include "Paper2DPlusFrameCuePreviewBlueprintLibrary.generated.h"

/** Blueprint authoring helpers that are available only to editor preview adapters. */
UCLASS()
class PAPER2DPLUSEDITOR_API UPaper2DPlusFrameCuePreviewBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Returns Cue when it matches CueClass, otherwise null. In Blueprint, a literal CueClass changes
	 * the return pin to that concrete cue type, so adapter authors never need an unsafe manual cast.
	 */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Frame Cue Preview",
		meta = (DisplayName = "Match Frame Cue for Preview", DeterminesOutputType = "CueClass"))
	static UPaper2DPlusCueBase* MatchFrameCueForPreview(
		UPaper2DPlusCueBase* Cue,
		TSubclassOf<UPaper2DPlusCueBase> CueClass,
		bool bExactClass = false);
};

