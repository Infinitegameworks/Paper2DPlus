// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusSettings.h"

const UPaper2DPlusSettings* UPaper2DPlusSettings::Get()
{
	return GetDefault<UPaper2DPlusSettings>();
}

FText UPaper2DPlusSettings::GetDescriptionForGroup(const FGameplayTag& Tag) const
{
	for (const FGroupDescriptionMapping& Mapping : GroupDescriptions)
	{
		if (Mapping.Group == Tag)
		{
			return Mapping.Description;
		}
	}
	return FText::GetEmpty();
}
