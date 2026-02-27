// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

PAPER2DPLUS_API DECLARE_LOG_CATEGORY_EXTERN(LogPaper2DPlus, Log, All);

class FPaper2DPlusModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
