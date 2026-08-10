#pragma once

#include "CoreMinimal.h"

class FUICommandList;

namespace Paper2DPlusProfileEditorToolbar
{
	extern const FName OwnerName;
	extern const FName PlaySectionName;

	void Install(
		FName ToolbarMenuName,
		const TSharedRef<FUICommandList>& ToolkitCommands);
}
