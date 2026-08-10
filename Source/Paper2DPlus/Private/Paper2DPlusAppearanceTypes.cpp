// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusAppearanceTypes.h"

bool FPaper2DPlusAppearanceDescriptor::operator==(
	const FPaper2DPlusAppearanceDescriptor& Other) const
{
	return SemanticVersion == Other.SemanticVersion
		&& DeliveryMode == Other.DeliveryMode
		&& ActiveLayerIds == Other.ActiveLayerIds;
}
