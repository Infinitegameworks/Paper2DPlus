// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"

/** Remappable Character Profile command surface for the transient direction wheel. */
class PAPER2DPLUSEDITOR_API FPaper2DPlusDirectionalAnimationCommands final
	: public TCommands<FPaper2DPlusDirectionalAnimationCommands>
{
public:
	FPaper2DPlusDirectionalAnimationCommands();

	virtual void RegisterCommands() override;

	/** Open the shared Character Profile direction wheel. Toolkit wiring owns its lifecycle. */
	TSharedPtr<FUICommandInfo> OpenDirectionWheel;
};
