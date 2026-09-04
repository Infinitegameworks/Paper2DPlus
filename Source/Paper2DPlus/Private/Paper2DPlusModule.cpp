// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusModule.h"
#include "Paper2DPlusDebugOverlay.h"
#if WITH_EDITOR
#include "FrameCues/Paper2DPlusFrameCueBlueprint.h"
#endif
#include "GameplayTagsManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/CoreDelegates.h"
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

		// Ship the recommended gameplay-tag taxonomy with the plugin: Config/Tags/*.ini registers as a
		// normal tag source so every consuming project sees the same Animation/Phase tree without copying
		// entries into its project DefaultGameplayTags.ini. TIMING (measured on 5.8, ordering differs on
		// 5.0): this module loads PostConfigInit, where the tag manager (a UObject) cannot be constructed
		// ("Object is not packaged" fatal), and even at ObjectSystemReady class reflection has not been
		// processed yet, so UGameplayTagsList::LoadConfig silently reads ZERO rows and the source is
		// permanently marked loaded. OnAllModuleLoadingPhasesComplete is the stable 5.0-5.8 hook: it fires
		// once all module loading phases finish (reflection + config ready) and before FEngineLoop::Init
		// loads any map/content in editor, cooked-game, and commandlet flows. AddTagIniSearchPath itself
		// handles a pre-built tree (adds rows + HandleGameplayTagTreeChanged) and is idempotent.
		FCoreDelegates::OnAllModuleLoadingPhasesComplete.AddLambda(
			[TagsIniDir = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Config"), TEXT("Tags"))]()
			{
				UGameplayTagsManager::Get().AddTagIniSearchPath(TagsIniDir);
			});
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
