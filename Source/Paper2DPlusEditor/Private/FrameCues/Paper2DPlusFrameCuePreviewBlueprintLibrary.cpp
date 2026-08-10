// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "FrameCues/Paper2DPlusFrameCuePreviewBlueprintLibrary.h"

UPaper2DPlusCueBase* UPaper2DPlusFrameCuePreviewBlueprintLibrary::MatchFrameCueForPreview(
	UPaper2DPlusCueBase* Cue,
	TSubclassOf<UPaper2DPlusCueBase> CueClass,
	bool bExactClass)
{
	const UClass* DesiredClass = CueClass.Get();
	if (!IsValid(Cue) || !DesiredClass)
	{
		return nullptr;
	}

	const bool bMatches = bExactClass
		? Cue->GetClass() == DesiredClass
		: Cue->IsA(DesiredClass);
	return bMatches ? Cue : nullptr;
}

