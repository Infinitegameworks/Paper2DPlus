// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusModule.h"
#include "Paper2DPlusDebugOverlay.h"
#if WITH_EDITOR
#include "FrameCues/Paper2DPlusFrameCueBlueprint.h"
#endif
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"

/** FPaper2DPlusModule — Plugin startup/shutdown and log category registration. */

DEFINE_LOG_CATEGORY(LogPaper2DPlus);

#define LOCTEXT_NAMESPACE "FPaper2DPlusModule"

void FPaper2DPlusModule::StartupModule()
{
	// Register before global shaders initialize (the runtime module loads PostConfigInit). The source shader ships
	// as code under Shaders/, not as a plugin content asset; FilterPlugin.ini /Shaders/... stages it for Fab/package builds.
	if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Paper2DPlus")))
	{
		AddShaderSourceDirectoryMapping(
			TEXT("/Plugin/Paper2DPlus"),
			FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders")));
	}
	else
	{
		UE_LOG(LogPaper2DPlus, Error,
			TEXT("Paper2DPlus shader directory could not be registered; Runtime Customizable will retain exact live layers."));
	}

	// Global, console-driven hitbox/frame-data debug overlay (no per-actor component).
	FPaper2DPlusDebugOverlay::Init();
}

void FPaper2DPlusModule::ShutdownModule()
{
#if WITH_EDITOR
	UPaper2DPlusFrameCueBlueprint::
		ResolveAllAutomaticDurableSaveTransactionsForShutdown();
#endif
	FPaper2DPlusDebugOverlay::Shutdown();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FPaper2DPlusModule, Paper2DPlus)
