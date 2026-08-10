// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UPaperSprite;

namespace Paper2DPlusSpriteSourceUtils
{
	/** Read a sprite's BGRA8 editor source rectangle into RGBA colors. Fails closed for unsupported source data. */
	PAPER2DPLUSEDITOR_API bool ReadSourceRegion(
		UPaperSprite* Sprite,
		TArray<FColor>& OutPixels,
		int32& OutWidth,
		int32& OutHeight);
}
