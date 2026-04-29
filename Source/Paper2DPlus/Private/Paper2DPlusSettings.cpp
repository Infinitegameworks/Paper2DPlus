// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusSettings.h"

/** UPaper2DPlusSettings — Project-wide plugin settings singleton. */

const UPaper2DPlusSettings* UPaper2DPlusSettings::Get()
{
	return GetDefault<UPaper2DPlusSettings>();
}

FText UPaper2DPlusSettings::GetDescriptionForTag(const FGameplayTag& Tag) const
{
	for (const FTagMappingDescription& Mapping : TagMappingDescriptions)
	{
		if (Mapping.Group == Tag)
		{
			return Mapping.Description;
		}
	}
	return FText::GetEmpty();
}
